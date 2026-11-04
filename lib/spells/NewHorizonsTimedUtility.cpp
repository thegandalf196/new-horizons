/*
 * NewHorizonsTimedUtility, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsTimedUtility.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsTheChosen.h"
#include "NewHorizonsTimedStatusPlan.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsMetamagicRefresh.h"
#include "NewHorizonsDivineRetribution.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../battle/StackEffectRefresh.h"
#include "../networkPacks/SetStackEffect.h"
#include "../bonuses/BonusParameters.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusCustomTypes.h"
namespace newHorizonsMagic
{
int32_t timedLightEffectDuration(const spells::Mechanics & mechanics)
{
	const auto * spell = mechanics.getSpellId().toSpell();
	if(spell->getJsonKey() == newHorizonsTheChosen::SPELL_KEY)
		return mechanics.getTheChosenDuration();
	if(mechanics.usesNewHorizonsMagicV3() && spell->getJsonKey() == "new-horizons:crusade")
	{
		const auto * hero = mechanics.getHeroCaster();
		const bool crusader = hero && hero->hasActivePerk("new-horizons:lightMagic",
			"new-horizons:lightMagic.crusader");
		return mechanics.adjustEffectDuration(3) + static_cast<int32_t>(crusader);
	}
	return mechanics.getEffectDuration();
}

namespace
{
std::optional<Bonus> prospectiveProtection(const spells::Mechanics & m, const battle::Unit & unit)
{
	const auto * family = effectSpellForCast(m);
	const auto * actual = m.getSpellId().toSpell();
	if(!family || !actual || !m.usesNewHorizonsMagicV3()) return std::nullopt;
	const auto & key = family->getJsonKey();
	BonusType type;
	if(key == "new-horizons:holyArmor") type = BonusType::SPELL_DAMAGE_REDUCTION;
	else if(key == "new-horizons:heavenlyGale") type = BonusType::HEAVENLY_GALE;
	else if(key == "new-horizons:guardianSpirit") type = BonusType::GUARDIAN_SPIRIT;
	else if(key == "new-horizons:divineRetribution") type = BonusType::DIVINE_RETRIBUTION;
	else if(key == "core:bless") type = BonusType::ALWAYS_MAXIMUM_DAMAGE;
	else return std::nullopt;
	// Only the authored one-record native timed producers are modeled here.
	// Additional/custom producers cannot be inferred from a familiar family name.
	const auto & info = actual->getLevelInfo(std::clamp(m.getEffectLevel(), 0, 3));
	const JsonNode * definition = nullptr;
	if(info.battleEffects.isNull())
	{
		if(info.effects.Struct().size() != 1 || !info.cumulativeEffects.Struct().empty()) return std::nullopt;
		definition = &info.effects.Struct().begin()->second;
	}
	else
	{
		if(!info.battleEffects.isStruct() || info.battleEffects.Struct().size() != 1) return std::nullopt;
		const auto & effect = info.battleEffects.Struct().begin()->second;
		if((effect["type"].String() != "timed" && effect["type"].String() != "core:timed")
			|| effect["cumulative"].Bool() || !effect["bonus"].isStruct()
			|| effect["bonus"].Struct().size() != 1) return std::nullopt;
		definition = &effect["bonus"].Struct().begin()->second;
	}
	if(!(*definition)["statusTags"].isNull() || !(*definition)["addInfo"].isNull()) return std::nullopt;
	auto value = configuredRoundBonusIdentity(*actual, m.getEffectLevel(), type);
	if(!value) return std::nullopt;
	value->source = BonusSource::SPELL_EFFECT;
	value->sid = BonusSourceID(family->getId());
	value->spellCasterOwner = m.getCasterColor();
	value->turnsRemain = static_cast<int16_t>(configuredTimedDurationForCast(m,
		value->turnsRemain, m.getEffectDuration(), true));
	if(type == BonusType::SPELL_DAMAGE_REDUCTION)
		value->val = holyArmorReductionForRecipient(m, value->val, unit);
	else if(type == BonusType::HEAVENLY_GALE)
		value->val = heavenlyGaleReductionForRecipient(m, unit);
	else if(type == BonusType::GUARDIAN_SPIRIT)
		value->val = m.getGuardianSpiritHitPoints(&unit); // Already recipient-scaled; packet transform excludes this magnitude.
	else if(type == BonusType::ALWAYS_MAXIMUM_DAMAGE) value->val = 0;
	else
	{
		value->val = m.getDivineRetributionCap(&unit); // Packet transform scales this cap once.
		const auto * hero = m.getHeroCaster();
		JsonNode parameters;
		parameters["retributionistPercent"].Integer() = hero && hero->hasActivePerk(
			"new-horizons:lightMagic", "new-horizons:lightMagic.retributionist") ? 120 : 100;
		if(const auto ratio = m.getRecipientDivineRetributionPayoutBasisPoints(&unit))
			parameters["payoutRatioBasisPoints"].Integer() = *ratio;
		value->parameters = std::make_shared<BonusParameters>(parameters);
	}
	SetStackEffect packet;
	packet.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{*value});
	newHorizonsMetamagic::adjustAppliedBonuses(m.getMetamagicCastProfile(), *m.battle(),
		m.getSpellId(), family->getId(), {unit.unitId()}, packet);
	return packet.toUpdate.front().second.front();
}

int32_t retributionRider(const Bonus & value)
{
	const auto parameters = value.parameters ? value.parameters->toJsonNode() : JsonNode{};
	return parameters.isStruct() && parameters.Struct().contains("retributionistPercent")
		? static_cast<int32_t>(parameters["retributionistPercent"].Integer()) : 100;
}
bool improvesProtection(const Bonus & incoming, const Bonus & previous)
{
	// A weaker replacing amount with a longer timer is an uncertain tradeoff,
	// not a strictly improved native transition for an automatic activation.
	if(incoming.val < previous.val || incoming.turnsRemain < previous.turnsRemain) return false;
	bool improved = incoming.val > previous.val || incoming.turnsRemain > previous.turnsRemain;
	if(incoming.type == BonusType::DIVINE_RETRIBUTION)
	{
		const auto ratio = newHorizonsDivineRetribution::payoutRatioBasisPoints(incoming);
		const auto oldRatio = newHorizonsDivineRetribution::payoutRatioBasisPoints(previous);
		const auto rider = retributionRider(incoming), oldRider = retributionRider(previous);
		if(ratio < oldRatio || rider < oldRider) return false;
		improved |= ratio > oldRatio || rider > oldRider;
	}
	return improved;
}
}

std::optional<bool> usefulTimedLightProtection(const spells::Mechanics & m, const battle::Unit & unit)
{
	const auto incoming = prospectiveProtection(m, unit);
	if(!incoming) return std::nullopt;
	if(incoming->type == BonusType::GUARDIAN_SPIRIT)
	{
		const auto remaining = unit.getGuardianSpiritHitPoints();
		const auto rounds = unit.getGuardianSpiritRoundsRemaining();
		return incoming->val >= remaining && incoming->turnsRemain >= rounds
			&& (incoming->val > remaining || incoming->turnsRemain > rounds);
	}
	const auto local = m.battle()->getBattle()->getLocalSpellEffects(unit.unitId());
	SetStackEffect packet;
	packet.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{*incoming});
	// Preserve the native current-policy magnitude replacement and timer max;
	// historical ordinary refresh retains its old magnitude.
	const bool explicitReplacement = incoming->type == BonusType::DIVINE_RETRIBUTION
		|| incoming->type == BonusType::ALWAYS_MAXIMUM_DAMAGE;
	if(!explicitReplacement)
		newHorizonsMetamagic::replaceRecalculatedSpellBonuses(m.getMetamagicCastProfile(), *m.battle(),
			m.getSpellId(), effectSpellForCast(m)->getId(), {unit.unitId()}, packet);
	bool matched = false;
	for(const auto & previous : local)
		if(stackEffectRefresh::matchesIdentity(*incoming, &previous))
		{
			matched = true;
			auto result = previous;
			if(explicitReplacement) result = *incoming; // Native explicit removal/replacement.
			else if(!packet.toAdd.empty()) result = packet.toAdd.front().second.front();
			else stackEffectRefresh::refreshFields(result, *incoming);
			if(improvesProtection(result, previous)) return true;
		}
	if(matched) return false;
	// Ordinary legacy updates cannot refresh a matching inherited-only source.
	return explicitReplacement || incoming->hasStatusMetadata()
		|| !unit.hasBonus(stackEffectRefresh::legacyCandidateSelector(*incoming));
}

std::optional<int32_t> configuredHolyArmorBase(const CSpell & spell, int32_t effectLevel)
{
	std::optional<int32_t> result;
	const auto & effects = spell.getLevelInfo(std::clamp(effectLevel, 0, 3)).battleEffects;
	for(const auto & [effectName, effect] : effects.Struct())
	{
		if(effect["type"].String() != "timed" && effect["type"].String() != "core:timed")
			continue;
		for(const auto & [name, definition] : effect["bonus"].Struct())
		{
			// Runtime decode reads resolved identities. The content loader's parseBonus
			// registers deferred identifier callbacks and is not a read-only UI API.
			const auto type = static_cast<BonusType>(BonusTypeID::decode(definition["type"].String()));
			if(type != BonusType::SPELL_DAMAGE_REDUCTION)
				continue;
			const auto subtype = decodeBonusSubtype(type, definition["subtype"].String());
			if(result || subtype != BonusSubtypeID(SpellSchool::ANY))
				return std::nullopt;
			// Same numeric conversion used by the existing typed Bonus loader.
			result = static_cast<int32_t>(definition["val"].Float());
		}
	}
	return result;
}

namespace
{
int64_t aegisPowerTerm(int64_t numerator, int32_t divisor, int32_t coefficient, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	if(crownAndAltar < 0 || crownAndAltar > 1000)
		throw std::invalid_argument("Invalid Holy Armor recipient multiplier");
	if(aegis)
	{
		numerator *= 120;
		divisor *= 100;
	}
	if(crownAndAltar != 0)
	{
		numerator *= 100 + crownAndAltar;
		divisor *= 100;
	}
	// Current Holy Armor deliberately has no generic non-damage specialty rider;
	// the original recipient scaler admits that rider only for Crusade.
	return spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		numerator, divisor, coefficient, empower);
}
}
int64_t holyArmorReduction(int32_t configuredBase, int32_t power, int32_t coefficient, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	return std::min<int64_t>(HOLY_ARMOR_MAXIMUM_REDUCTION, static_cast<int64_t>(configuredBase)
		+ aegisPowerTerm(power, 5, coefficient, empower, aegis, crownAndAltar));
}
int64_t heavenlyGaleReductionBasisPoints(int32_t power, int32_t coefficient, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	return std::min<int64_t>(HEAVENLY_GALE_MAXIMUM_REDUCTION_BASIS_POINTS, 5000
		+ aegisPowerTerm(15LL * power, 1, coefficient, empower, aegis, crownAndAltar));
}

int32_t forgetfulnessBaseDuration(int32_t power, int32_t coefficient, int32_t empower)
{
	const auto component = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		std::max(0, power), 80, coefficient, empower);
	return static_cast<int32_t>(std::min<int64_t>(3, 1 + component));
}

int32_t landMineCountForHero(int32_t power, int32_t coefficient, const CGHeroInstance * hero)
{
	return landMineHexCount(power, coefficient) + static_cast<int>(hero
		&& hero->hasActivePerk(std::string(HAVOC_MAGIC_SKILL), "new-horizons:havocMagic.mineLayer"));
}

int32_t hexOfPainDamageSharePercent()
{
	return 10;
}

int32_t durationWithCastExtensions(int32_t base, bool extend, bool echoedDuration)
{
	const int extra = static_cast<int>(extend && base > 0) + static_cast<int>(echoedDuration);
	if(base < std::numeric_limits<int32_t>::max())
		base += std::min(extra, std::numeric_limits<int32_t>::max() - std::max(base, 0));
	return base;
}
}
