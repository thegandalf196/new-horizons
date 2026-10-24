/*
 * NewHorizonsSwampSeerPresentation.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSwampSeer.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
bool resolveSwampSeerPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:tiva")
		return false;
	const auto rule = swampSeerRules(development);
	if(!rule)
		return false;
	result.name = "Swamp Seer";
	result.artwork = "newHorizons/heroSpecialties/TivaSwampSeer";
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	result.description = "Learning's core experience bonus increases by " + std::to_string(rule->learningPercentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->learningMaximumPercent)
		+ "% of that contribution. Other experience modifiers are unchanged. While Learning is active, gain "
		+ std::to_string(rule->studyExperiencePerMagicPrimaryPoint)
		+ " raw experience per permanent Knowledge or Spell Power point actually received from a Garden of Revelation, "
		"Star Axis, Library of Enlightenment or School of Magic, before ordinary Learning bonuses. "
		"Payment, visit limits and attribute caps remain. Artifacts, temporary bonuses, physical attributes and level-up gains do not count. "
		"No Historian perk is required or granted; this study experience does not receive its bonus.";
	if(capturedHero && capturedHero->getSecSkillLevel(*result.skill) == MasteryLevel::NONE)
		result.description += " Learning is not currently learned; study experience is inactive.";
	return true;
}
}
