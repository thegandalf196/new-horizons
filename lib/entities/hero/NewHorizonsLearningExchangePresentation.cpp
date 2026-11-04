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
	result.summary = "Gain more experience through Learning.";
	result.artwork = "newHorizons/heroSpecialties/" + std::string(outgoing ? "SanyaEcumenicalScholar" : "GeonOccultScholar");
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	result.description = "Learning's core experience bonus increases by " + std::to_string(rule->learningPercentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->learningMaximumPercent)
		+ "% of that contribution. Fractions round down; other experience modifiers are unchanged. ";
	if(capturedHero && capturedHero->getSecSkillLevel(*result.skill) == MasteryLevel::NONE)
		result.description += " Learning is not currently learned.";
	return true;
}
}
