/*
 * NewHorizonsLearningExchangePresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
namespace newHorizonsHeroes
{
bool resolveLearningExchangePresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const bool outgoing = hero.getJsonKey() == "core:sanya";
	if(!outgoing && hero.getJsonKey() != "core:geon")
		return false;
	const auto rule = learningExchangeSpecialtyRules(development, outgoing
		? LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR : LearningExchangeSpecialtyKind::OCCULT_SCHOLAR);
	if(!rule)
		return false;
	result.name = outgoing ? "Ecumenical Scholar" : "Occult Scholar";
	result.artwork.reset(); // Individual artwork selection/packaging is a separate content task.
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	result.description = "Learning's core experience bonus increases by " + std::to_string(rule->learningPercentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->learningMaximumPercent)
		+ "% of that contribution. Fractions round down; other experience modifiers are unchanged. ";
	if(newHorizonsMagic::rulesActive(magic)
		&& magic["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION)
		result.description += std::string(outgoing ? "In an ordinary Scholar meeting, this hero teaches up to "
			: "In an ordinary Scholar meeting, this hero learns up to ")
			+ std::to_string(outgoing ? rule->additionalSpellsTaught : rule->additionalSpellsReceived)
			+ " additional eligible spell(s) in that direction. Either participant must have active Scholar. "
			"Highest eligible level first; ties follow ordinary spellbook order. One meeting per hero pair per week. "
			"Coincident outgoing and incoming bonuses use the greater value, never their sum. No perk is granted.";
	else
		result.description += "The additional Scholar exchange is inactive under this game's legacy magic rules.";
	if(capturedHero && capturedHero->getSecSkillLevel(*result.skill) == MasteryLevel::NONE)
		result.description += " Learning is not currently learned.";
	return true;
}
}
