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
int32_t chaplainReserveRecovery(const CGHeroInstance * hero,
	const uint8_t beforeCompletedPairs, const uint8_t afterCompletedPairs)
{
	if(!hero || beforeCompletedPairs != 0 || afterCompletedPairs != 1
		|| hero->getFactionID() != FactionID::CASTLE
		|| !hero->usesPrimaryGrowth()
		|| !newHorizonsHeroes::usesRules(hero->getPrimaryGrowthRules())
		|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
		return 0;

	const auto factionSkill = newHorizonsHeroes::factionSkill(
		hero->getPrimaryGrowthRules(), FactionID::CASTLE);
	const auto divineMandateId = SecondarySkill::decode("new-horizons:divineMandate");
	if(!factionSkill || divineMandateId < 0 || *factionSkill != SecondarySkill(divineMandateId)
		|| hero->getSecSkillLevel(*factionSkill) == 0)
		return 0;

	return hero->hasActivePerk("new-horizons:divineMandate",
		"new-horizons:divineMandate.chaplainSReserve") ? 3 : 0;
}
}
