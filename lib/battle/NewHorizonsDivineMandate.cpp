/*
 * NewHorizonsDivineMandate.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsDivineMandate.h"

#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"

namespace newHorizonsDivineMandate
{
namespace
{
bool hasActiveDivineMandate(const CGHeroInstance * hero)
{
	if(!hero || hero->getFactionID() != FactionID::CASTLE
		|| !hero->usesPrimaryGrowth()
		|| !newHorizonsHeroes::usesRules(hero->getPrimaryGrowthRules())
		|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
		return false;

	const auto factionSkill = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), FactionID::CASTLE);
	const auto divineMandateId = SecondarySkill::decode("new-horizons:divineMandate");
	return factionSkill && divineMandateId >= 0 && *factionSkill == SecondarySkill(divineMandateId)
		&& hero->getSecSkillLevel(*factionSkill) != 0;
}
}

int32_t chaplainReserveRecovery(const CGHeroInstance * hero,
	const uint8_t beforeCompletedPairs, const uint8_t afterCompletedPairs)
{
	if(beforeCompletedPairs != 0 || afterCompletedPairs != 1 || !hasActiveDivineMandate(hero))
		return 0;

	return hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.chaplainSReserve") ? 3 : 0;
}

int32_t sacredCommandEfficiencyBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.sacredCommand") ? 10 : 0;
}

int32_t knightlySequenceOrderBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.knightlySequence") ? 5 : 0;
}

int32_t knightlySequenceSpellCostReduction(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.knightlySequence") ? 2 : 0;
}

int32_t consecratedCastingBonusPercent(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.consecratedCasting") ? 10 : 0;
}

bool hasPurifyingMandatePerk(const CGHeroInstance * hero)
{
	return hasActiveDivineMandate(hero) && hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.purifyingMandate");
}
}
