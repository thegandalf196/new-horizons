/*
 * CGHeroCreatureSpecialties.cpp, part of VCMI engine / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/NewHorizonsPassiveSpecialtyBonuses.h"
#include "../CCreatureHandler.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include <array>
#include <string_view>
namespace
{
std::string creatureLineSpecialtyMarker(HeroTypeID heroType, CreatureID creature, std::string_view stat)
{
	return "new-horizons:creature-line-specialty:" + std::to_string(heroType.getNum()) + ":"
		+ std::to_string(creature.getNum()) + ":" + std::string(stat);
}

}

void CGHeroInstance::validateDefaultCreatureLineSerialization(bool supported) const
{
	newHorizonsHeroes::validateDefaultCreatureLineSerialization(primaryGrowthRules, supported);
	if(primaryGrowthRules.isStruct() && primaryGrowthRules.Struct().contains("creatureLineSpecialtyTarget"))
	{
		const auto target = newHorizonsHeroes::defaultCreatureLineTarget(primaryGrowthRules, getHeroTypeID());
		if(!target || !getHeroType()->creatureLineSpecialtyAlias
			|| getHeroType()->creatureLineSpecialtyAlias->creature != CreatureID(CreatureID::PSYCHIC_ELEMENTAL))
			throw std::runtime_error("Captured Wisp specialty belongs only to the exact native Psychic producer");
		const auto original = getHeroType()->creatureLineSpecialtyAlias->creature;
		const std::array<std::string, 4> staleMarkers = {
			creatureLineSpecialtyMarker(getHeroTypeID(), original, "speed"),
			creatureLineSpecialtyMarker(getHeroTypeID(), original, "initiative"),
			creatureLineSpecialtyMarker(getHeroTypeID(), original, "attack"),
			creatureLineSpecialtyMarker(getHeroTypeID(), original, "defense")
		};
		if(std::ranges::any_of(getExportedBonusList(), [&staleMarkers](const auto & bonus)
		{
			return bonus && std::ranges::find(staleMarkers, bonus->stacking) != staleMarkers.end();
		}))
			throw std::runtime_error("Captured Wisp specialty cannot retain Psychic target markers");
	}
}

CreatureID CGHeroInstance::getCreatureLineSpecialtyTarget() const
{
	validateDefaultCreatureLineSerialization(true);
	if(primaryGrowthRules.isStruct() && primaryGrowthRules.Struct().contains("creatureLineSpecialtyTarget"))
		return *newHorizonsHeroes::defaultCreatureLineTarget(primaryGrowthRules, getHeroTypeID());
	const auto * hero = getHeroType();
	return hero && hero->creatureLineSpecialtyAlias ? hero->creatureLineSpecialtyAlias->creature : CreatureID();
}


void CGHeroInstance::refreshCreatureLineSpecialtyBonuses(bool createIfMissing)
{
	if(createIfMissing)
	{
		const auto passiveBonuses = newHorizonsHeroes::passiveCreatureSpecialtyBonuses(*this);
		// One family bundle may contain several rows with the same stacking identity.
		// Remove the old bundle before adding any of its replacement rows.
		if(!passiveBonuses.empty())
			removeBonuses(CSelector([&passiveBonuses](const Bonus * existing)
			{
				return existing && std::ranges::any_of(passiveBonuses, [existing](const auto & bonus)
				{ return existing->source == bonus->source && existing->sid == bonus->sid
					&& existing->stacking == bonus->stacking; });
			}));
		for(const auto & bonus : passiveBonuses)
			addNewBonus(bonus);
	}
	if(createIfMissing)
		if(auto bonus = newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*this))
		{
			removeBonuses(CSelector([&bonus](const Bonus * existing)
			{ return existing && existing->source == bonus->source && existing->sid == bonus->sid
				&& existing->stacking == bonus->stacking; }));
			addNewBonus(bonus);
		}

	const auto rules = newHorizonsHeroes::creatureLineSpecialtyRules(primaryGrowthRules);
	const CHero * heroType = getHeroType();
	if(!rules || !heroType || !heroType->creatureLineSpecialtyAlias)
		return;

	const CreatureID creature = getCreatureLineSpecialtyTarget();
	const std::array<std::string, 4> markers = {
		creatureLineSpecialtyMarker(heroType->getId(), creature, "speed"),
		creatureLineSpecialtyMarker(heroType->getId(), creature, "initiative"),
		creatureLineSpecialtyMarker(heroType->getId(), creature, "attack"),
		creatureLineSpecialtyMarker(heroType->getId(), creature, "defense")
	};
	const auto & existingBonuses = getExportedBonusList();
	const bool hasMarkers = std::ranges::any_of(existingBonuses, [&markers](const auto & bonus)
	{
		return bonus && std::ranges::find(markers, bonus->stacking) != markers.end();
	});
	if(!createIfMissing && !hasMarkers)
		return;

	const CCreature * specCreature = creature.toCreature();
	if(!specCreature)
		return;

	removeBonuses(CSelector([markers](const Bonus * bonus)
	{
		return bonus && std::ranges::find(markers, bonus->stacking) != markers.end();
	}));

	if(newHorizonsHeroes::replacesGenericCreatureSpecialty(this))
		return;

	for(auto & bonus : newHorizonsHeroes::creatureLineSpecialtyBonuses(*this, *rules, creature, markers))
		addNewBonus(bonus);
}

