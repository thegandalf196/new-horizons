/*
 * NewHorizonsSpellAvailability.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMagic.h"
#include "../constants/StringConstants.h"
#include "../GameLibrary.h"
#include "../callback/IGameInfoCallback.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "CSpell.h"
#include "CSpellHandler.h"

namespace newHorizonsMagic
{
namespace
{
struct SavedSpellVariant
{
	SpellID base;
	std::string skill;
	std::string perk;
	int powerPercent;
};

const CSpell * commonHeroSpell(SpellID spell)
{
	const auto id = spell.getNum();
	if(id < 0 || !LIBRARY || !LIBRARY->spellh
		|| static_cast<size_t>(id) >= LIBRARY->spellh->objects.size())
		return nullptr;

	const auto & definition = LIBRARY->spellh->objects.at(id);
	if(!definition || !definition->isCommonHeroSpell())
		return nullptr;

	return definition.get();
}

const JsonNode * savedSpellRow(const JsonNode & rules, const std::string & identity)
{
	if(!rules.isStruct() || !rules["spells"].isStruct())
		return nullptr;

	const auto found = rules["spells"].Struct().find(identity);
	if(found == rules["spells"].Struct().end() || !found->second.isStruct())
		return nullptr;

	return &found->second;
}

bool savedSpellRowIsActive(const JsonNode & row)
{
	if(!row.isStruct())
		return false;

	const auto active = row.Struct().find("active");
	if(active == row.Struct().end())
		return true; // Rows without a marker retain the saved roster's default-active semantics.

	return active->second.isBool() && active->second.Bool();
}

std::optional<SavedSpellVariant> savedV3ActiveVariant(const JsonNode & rules, SpellID spell)
{
	if(!rules.isStruct()
		|| rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;

	const auto * definition = commonHeroSpell(spell);
	if(!definition)
		return std::nullopt;

	const auto * row = savedSpellRow(rules, definition->getJsonKey());
	if(!row)
		return std::nullopt;

	const auto active = row->Struct().find("active");
	const auto ordinaryAcquisition = row->Struct().find("ordinaryAcquisition");
	const auto variant = row->Struct().find("variant");
	if(active == row->Struct().end() || !active->second.isBool() || !active->second.Bool()
		|| ordinaryAcquisition == row->Struct().end() || !ordinaryAcquisition->second.isBool()
		|| ordinaryAcquisition->second.Bool()
		|| variant == row->Struct().end() || !variant->second.isStruct())
		return std::nullopt;

	const auto & data = variant->second;
	const auto base = data.Struct().find("base");
	const auto skill = data.Struct().find("skill");
	const auto perk = data.Struct().find("perk");
	const auto powerPercent = data.Struct().find("powerPercent");
	if(base == data.Struct().end() || !base->second.isString()
		|| skill == data.Struct().end() || !skill->second.isString()
		|| perk == data.Struct().end() || !perk->second.isString()
		|| powerPercent == data.Struct().end()
		|| powerPercent->second.getType() != JsonNode::JsonType::DATA_INTEGER
		|| (powerPercent->second.Integer() != 100
			&& !(powerPercent->second.Integer() == 60 && base->second.String() == "core:slow")))
		return std::nullopt;

	const auto & baseIdentity = base->second.String();
	const auto & skillIdentity = skill->second.String();
	const auto & perkIdentity = perk->second.String();
	const auto baseSeparator = baseIdentity.find(':');
	const auto skillSeparator = skillIdentity.find(':');
	const auto perkSeparator = perkIdentity.find(':');
	if(baseSeparator == std::string::npos || baseSeparator == 0 || baseSeparator + 1 == baseIdentity.size()
		|| skillSeparator == std::string::npos || skillSeparator == 0 || skillSeparator + 1 == skillIdentity.size()
		|| perkSeparator == std::string::npos || perkSeparator == 0 || perkSeparator + 1 == perkIdentity.size()
		|| !perkIdentity.starts_with(skillIdentity + '.')
		|| perkIdentity.size() == skillIdentity.size() + 1)
		return std::nullopt;

	const auto skillId = SecondarySkill::decode(skillIdentity);
	if(skillId < 0)
		return std::nullopt;

	const SpellID baseSpell(SpellID::decode(baseIdentity));
	const auto * baseDefinition = commonHeroSpell(baseSpell);
	if(!baseDefinition || baseSpell == spell || baseDefinition->getJsonKey() != baseIdentity)
		return std::nullopt;
	const auto * baseRow = savedSpellRow(rules, baseDefinition->getJsonKey());
	if(!baseRow || !savedSpellRowIsActive(*baseRow) || baseRow->Struct().contains("variant"))
		return std::nullopt;

	return SavedSpellVariant{baseSpell, skillIdentity, perkIdentity,
		static_cast<int>(powerPercent->second.Integer())};
}
}

bool spellBelongsToRules(const JsonNode & rules, const std::string & scopedIdentity, bool commonHeroSpell)
{
	if(!commonHeroSpell)
		return true; // Existing creature/special-ability rules remain responsible.
	const auto separator = scopedIdentity.find(':');
	if(separator == std::string::npos || separator == 0 || separator + 1 == scopedIdentity.size())
		throw std::runtime_error("Spell availability requires a scoped identity");
	if(rules.isNull() || (rules.isStruct() && rules.Struct().empty()))
		return !scopedIdentity.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':');
	if(!rules.isStruct() || !rules["spells"].isStruct())
		throw std::runtime_error("Spell availability requires a saved spell roster");
	const auto & savedSpells = rules["spells"].Struct();
	const auto found = savedSpells.find(scopedIdentity);
	if(found != savedSpells.end())
	{
		if(!found->second.isStruct())
			throw std::runtime_error("Saved spell roster entry must be an object");
		const auto active = found->second.Struct().find("active");
		if(active == found->second.Struct().end())
			return true; // Old snapshots had no marker and keep their saved spell.
		if(!active->second.isBool())
			throw std::runtime_error("Saved spell roster active marker must be boolean");
		return active->second.Bool();
	}
	return rules["adventureSpells"].isStruct()
		&& rules["adventureSpells"].Struct().contains(scopedIdentity);
}

bool spellAvailableForOrdinaryAcquisition(const JsonNode & rules,
	const std::string & scopedIdentity, bool commonHeroSpell)
{
	if(!commonHeroSpell)
		return false;

	bool ordinaryAcquisition = true;
	if(rules.isStruct() && rules["spells"].isStruct())
	{
		const auto found = rules["spells"].Struct().find(scopedIdentity);
		if(found != rules["spells"].Struct().end())
		{
			if(!found->second.isStruct())
				throw std::runtime_error("Saved spell roster entry must be an object");
			const auto marker = found->second.Struct().find("ordinaryAcquisition");
			if(marker != found->second.Struct().end())
			{
				if(!marker->second.isBool())
					throw std::runtime_error("Saved spell roster ordinaryAcquisition marker must be boolean");
				ordinaryAcquisition = marker->second.Bool();
			}
		}
	}

	return spellBelongsToRules(rules, scopedIdentity, commonHeroSpell)
		&& ordinaryAcquisition;
}

bool spellAllowedBySavedRoster(const JsonNode & rules, SpellID spell)
{
	const auto id = spell.getNum();
	if(id < 0 || !LIBRARY || !LIBRARY->spellh || static_cast<size_t>(id) >= LIBRARY->spellh->objects.size())
		return false;
	const auto & definition = LIBRARY->spellh->objects.at(id);
	return definition && spellBelongsToRules(rules, definition->getJsonKey(), definition->isCommonHeroSpell());
}

bool spellAvailableForOrdinaryAcquisition(const JsonNode & rules, SpellID spell)
{
	const auto id = spell.getNum();
	if(id < 0 || !LIBRARY || !LIBRARY->spellh || static_cast<size_t>(id) >= LIBRARY->spellh->objects.size())
		return false;
	const auto & definition = LIBRARY->spellh->objects.at(id);
	return definition && definition->isCommonHeroSpell()
		&& spellAvailableForOrdinaryAcquisition(rules, definition->getJsonKey(), true);
}

bool spellAllowedByWorldRoster(const IGameInfoCallback & world, SpellID spell)
{
	return spellAllowedBySavedRoster(world.getMagicRules(), spell);
}

bool spellAllowedByBattleRoster(const CBattleInfoCallback & battle, SpellID spell)
{
	const auto * state = battle.getBattle();
	return state && spellAllowedBySavedRoster(state->getMagicRules(), spell);
}

SpellID spellVariantBase(const JsonNode & rules, SpellID spell)
{
	const auto variant = savedV3ActiveVariant(rules, spell);
	return variant ? variant->base : spell;
}

int spellVariantPowerPercent(const JsonNode & rules, SpellID spell)
{
	const auto variant = savedV3ActiveVariant(rules, spell);
	return variant ? variant->powerPercent : 100;
}

bool hasDistinctMassSlow(const JsonNode & rules)
{
	const SpellID spell(SpellID::decode("new-horizons:massSlow"));
	const auto variant = savedV3ActiveVariant(rules, spell);
	return variant && variant->base == SpellID::SLOW && variant->powerPercent == 60;
}

bool variantGrantAvailable(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell)
{
	if(!hero || !hero->hasSpellbook())
		return false;

	const auto variant = savedV3ActiveVariant(rules, spell);
	if(!variant)
		return false;

	return hero->hasActivePerk(variant->skill, variant->perk);
}
}
