/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Timed.h"
#include "../../ISpellMechanics.h"
#include "../../CSpell.h"
#include "../../../battle/Unit.h"
#include "../../NewHorizonsSpellAvailability.h"
#include "../../SpellBonusProvenance.h"
#include "../../NewHorizonsMetamagicEffects.h"
#include "../../../networkPacks/SetStackEffect.h"
#include "../../../battle/CBattleInfoCallback.h"
#include "../../../GameLibrary.h"
#include "../../../modding/IdentifierStorage.h"
#include "../../../modding/ModScope.h"
#include <vcmi/spells/Spell.h>
#include <cmath>
#include <limits>
#include <set>

namespace spells::effects::native
{
namespace
{
template<class T> bool enumValue(const JsonNode & value, const std::map<std::string, T> & known, T & result)
{
	if(value.isNull()) return true;
	if(!value.isString()) return false;
	const auto found = known.find(value.String());
	if(found == known.end()) return false;
	result = found->second; return true;
}

std::optional<std::string> untypedIdentifier(std::string name, std::string_view expected)
{
	const auto scope = name.find(':');
	const size_t start = scope == std::string::npos ? 0 : scope + 1;
	const auto dot = name.find('.', start);
	if(dot != std::string::npos)
	{
		if(std::string_view(name).substr(start, dot - start) != expected) return std::nullopt;
		name.erase(start, dot - start + 1);
		if(name.find('.', start) != std::string::npos) return std::nullopt;
	}
	return name;
}

bool descriptorShape(const JsonNode & descriptor)
{
	if(!descriptor.isStruct()) return false;
	for(const auto * key : {"type", "subtype", "valueType", "effectRange", "duration", "targetSourceType", "stacking", "statusIdentity"})
		if(!descriptor[key].isNull() && !descriptor[key].isString()) return false;
	for(const auto * key : {"val", "turns"})
	{
		const auto & field = descriptor[key];
		if(field.isNull()) continue;
		if(!field.isNumber() || !std::isfinite(field.Float())) return false;
		const double minimum = std::string_view(key) == "val" ? std::numeric_limits<int32_t>::min() : std::numeric_limits<int16_t>::min();
		const double maximum = std::string_view(key) == "val" ? std::numeric_limits<int32_t>::max() : std::numeric_limits<int16_t>::max();
		if(field.Float() < minimum || field.Float() > maximum) return false;
	}
	if(!descriptor["hidden"].isNull() && !descriptor["hidden"].isBool()) return false;
	if(!descriptor["statusTags"].isNull() && !descriptor["statusTags"].isVector()) return false;
	if(descriptor["statusTags"].isVector())
		for(const auto & tag : descriptor["statusTags"].Vector()) if(!tag.isString()) return false;
	for(const auto * key : {"updater", "limiters", "propagator", "propagationUpdater", "addInfo"})
		if(!descriptor[key].isNull()) return false;
	return true;
}

std::optional<Bonus> numericBonus(const JsonNode & descriptor, SpellID spell)
{
	if(!descriptorShape(descriptor)) return std::nullopt;
	static const std::set<std::string> fields{"type", "val", "turns", "subtype", "valueType", "effectRange",
		"duration", "sourceType", "sourceID", "targetSourceType", "stacking", "statusTags", "statusIdentity",
		"hidden", "description", "icon"};
	for(const auto & [key, value] : descriptor.Struct())
		if(!value.isNull() && !fields.contains(key)) return std::nullopt;
	if((!descriptor["stacking"].isNull() && !descriptor["stacking"].isString())
		|| (!descriptor["statusIdentity"].isNull() && !descriptor["statusIdentity"].isString())
		|| (!descriptor["hidden"].isNull() && !descriptor["hidden"].isBool())) return std::nullopt;
	if(!descriptor["type"].isString() || descriptor["sourceType"].String() != "SPELL_EFFECT"
		|| descriptor["sourceID"].String() != spell.toSpell()->getJsonKey()) return std::nullopt;
	const auto typeName = untypedIdentifier(descriptor["type"].String(), "bonus");
	if(!typeName) return std::nullopt;
	const auto type = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "bonus", *typeName, true);
	if(!type) return std::nullopt;
	Bonus result;
	result.type = static_cast<BonusType>(*type);
	// Scripts are execution capabilities, not ordinary numeric stat records.
	if(result.type == BonusType::COMBAT_EVENT_TRIGGER || result.type == BonusType::PHYSICAL_AFFLICTION) return std::nullopt;
	if((!descriptor["val"].isNull() && !descriptor["val"].isNumber())
		|| (!descriptor["turns"].isNull() && !descriptor["turns"].isNumber())) return std::nullopt;
	const double value = descriptor["val"].Float(), turns = descriptor["turns"].Float();
	if(!std::isfinite(value) || value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()
		|| !std::isfinite(turns) || turns < std::numeric_limits<int16_t>::min() || turns > std::numeric_limits<int16_t>::max()) return std::nullopt;
	result.val = static_cast<int32_t>(value); result.turnsRemain = static_cast<int16_t>(turns);
	if(!enumValue(descriptor["valueType"], bonusValueMap, result.valType)
		|| !enumValue(descriptor["effectRange"], bonusLimitEffect, result.effectRange)
		|| !enumValue(descriptor["duration"], bonusDurationMap, result.duration)
		|| !enumValue(descriptor["targetSourceType"], bonusSourceMap, result.targetSourceType)) return std::nullopt;
	const auto & subtype = descriptor["subtype"];
	if(subtype.isString())
	{
		const auto entity = bonusSubtypeEntityType(result.type);
		if(entity == EntityTypeEnum::NONE || entity == EntityTypeEnum::SCRIPT) return std::nullopt;
		const auto subtypeName = untypedIdentifier(subtype.String(), entityTypeName(entity));
		if(!subtypeName) return std::nullopt;
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), entityTypeName(entity), *subtypeName, true);
		if(!id) return std::nullopt;
		result.subtype = bonusSubtypeOf(entity, *id);
	}
	else if(!subtype.isNull()) return std::nullopt;
	result.source = BonusSource::SPELL_EFFECT; result.sid = BonusSourceID(spell);
	result.stacking = descriptor["stacking"].String(); result.hidden = descriptor["hidden"].Bool();
	result.statusIdentity = descriptor["statusIdentity"].String();
	if(!descriptor["statusTags"].isNull() && !descriptor["statusTags"].isVector()) return std::nullopt;
	if(descriptor["statusTags"].isVector()) for(const auto & tag : descriptor["statusTags"].Vector())
	{
		if(!tag.isString() || (tag.String() != "DEBUFF" && tag.String() != "NON_TRANSFERABLE")) return std::nullopt;
		result.statusTags.push_back(tag.String() == "DEBUFF" ? BonusStatusTag::DEBUFF : BonusStatusTag::NON_TRANSFERABLE);
	}
	if(!result.hasValidStatusMetadata()) return std::nullopt;
	return result;
}
}

std::optional<std::vector<Bonus>> Timed::preparedRecipientBonuses(const Mechanics * mechanics, const battle::Unit * unit) const
{
	if(!mechanics || !unit) return std::nullopt;
	// Validate scalar shapes before conversion/specialty code reads them.
	if(!parameters["bonus"].isNull() && !parameters["bonus"].isStruct()) return std::nullopt;
	for(const auto & [name, descriptor] : parameters["bonus"].Struct())
		if(!descriptorShape(descriptor)) return std::nullopt;
	const auto cast = prepareCast(mechanics);
	if(!cast.allowed) return std::nullopt;
	auto plan = prepareRecipient(mechanics, unit, cast);
	if(plan.prepared && (plan.prepared->limiter || plan.prepared->updater || plan.prepared->propagator
		|| plan.prepared->propagationUpdater || plan.prepared->parameters
		|| plan.prepared->type == BonusType::COMBAT_EVENT_TRIGGER)) return std::nullopt;
	const auto spell = newHorizonsMagic::effectSpellForCast(*mechanics)->getId();
	std::vector<Bonus> result;
	for(const auto & [name, descriptor] : plan.buffer)
	{
		if(plan.prepared && std::string_view(name) >= plan.preparedKey)
		{ result.push_back(*plan.prepared); plan.prepared.reset(); }
		if(plan.preparedKey.empty() || std::string_view(name) != plan.preparedKey)
		{
			auto bonus = numericBonus(descriptor, spell);
			if(!bonus) return std::nullopt;
			result.push_back(std::move(*bonus));
		}
	}
	if(plan.prepared) result.push_back(*plan.prepared);
	const auto side = mechanics->getEffectCasterSide();
	if(!mechanics->battle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)) return std::nullopt;
	// The packet recorder applies this pure transform once after materialization.
	// Use the same captured profile on numeric copies, without emitting a packet.
	SetStackEffect numeric;
	numeric.toAdd.emplace_back(unit->unitId(), std::move(result));
	newHorizonsMetamagic::adjustAppliedBonuses(mechanics->getMetamagicCastProfile(), *mechanics->battle(),
		mechanics->getSpellId(), spell, {unit->unitId()}, numeric);
	result = std::move(numeric.toAdd.front().second);
	for(auto & bonus : result)
		stampSpellBonusProvenance(bonus, mechanics->getSpellId(), spell,
			mechanics->battle()->sideToPlayer(side), mechanics->battle()->battleGetOwner(unit));
	return result;
}
}
