/*
 * SkillChoiceEligibility.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "SkillChoiceEligibility.h"
#include "NewHorizonsHeroRules.h"
#include "../../GameLibrary.h"
#include "../../CSkillHandler.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
bool isValidLevelUpSkillChoice(const IGameInfoCallback & info, const CGHeroInstance * hero, SecondarySkill skill)
{
	if(!hero || skill == SecondarySkill::NONE)
		return false;

	const auto & rules = hero->getPrimaryGrowthRules();
	const auto ownFactionSkill = newHorizonsHeroes::factionSkill(rules, hero->getFactionID());
	if(newHorizonsHeroes::isFactionSkill(rules, skill)
		&& (!ownFactionSkill || !newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), skill)))
		return false;
	if(ownFactionSkill && newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), skill))
	{
		const auto representative = std::find_if(hero->secSkills.begin(), hero->secSkills.end(), [&rules, hero](const auto & entry)
		{
			return newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), entry.first);
		});
		if(representative != hero->secSkills.end() && representative->first != skill)
			return false;
	}

	const int currentRank = hero->getSecSkillLevel(skill);
	if(currentRank >= MasteryLevel::EXPERT)
		return false;
	const auto & perkState = hero->getPerkState();
	const auto * skillEntity = LIBRARY->skillh->getById(skill);
	if(!skillEntity || !perkState.canAdvanceSkillNormally(skillEntity->getJsonKey(), currentRank))
		return false;
	if(currentRank > MasteryLevel::NONE)
		return true;
	if(!hero->canLearnSkill())
		return false;

	// Own faction skills are intentionally absent from the legacy class
	// probability table. They are still legal as a new skill when the active
	// rules snapshot says so, subject to the map's allowed-skill roster.
	if(ownFactionSkill && *ownFactionSkill == skill)
		return info.isAllowed(skill);
	return hero->canLearnSkill(skill);
}
std::set<SecondarySkill> connoisseurSkillCandidates(const IGameInfoCallback & info,
	const CGHeroInstance & hero, const std::vector<SecondarySkill> & offeredSkills)
{
	std::set<SecondarySkill> candidates;
	for(const auto & definition : LIBRARY->skillh->objects)
	{
		if(!definition)
			continue;
		const auto skill = definition->getId();
		if(vstd::contains(offeredSkills, skill) || !isValidLevelUpSkillChoice(info, &hero, skill)
			|| newHorizonsHeroes::isExcludedSkill(hero.getPrimaryGrowthRules(), skill)
			|| newHorizonsHeroes::skillOfferWeight(hero.getPrimaryGrowthRules(), skill).value_or(0) <= 0)
			continue;
		candidates.insert(skill);
	}
	return candidates;
}
}
