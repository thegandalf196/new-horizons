/*
 * NewHorizonsLearningSpecialtyPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningSpecialties.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsSwampSeer.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
namespace newHorizonsHeroes
{
bool resolveLearningSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(resolveSwampSeerPresentation(hero, development, capturedHero, result)
		|| resolveLearningExchangePresentation(hero, development, magic, capturedHero, result))
		return true;
	const auto identity = hero.getJsonKey();
	if(identity != "core:nimbus" && identity != "core:oris")
		return false;
	const bool archivist = identity == "core:nimbus";
	const auto rule = learningSpecialtyRules(development, archivist
		? LearningSpecialtyKind::GRAVE_ARCHIVIST : LearningSpecialtyKind::SPIRIT_TEACHER);
	if(!rule)
		return false;
	result.name = archivist ? "Grave Archivist" : "Spirit Teacher";
	result.artwork = "newHorizons/heroSpecialties/" + std::string(archivist ? "NimbusGraveArchivist" : "OrisSpiritTeacher");
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	result.description = "Learning's core experience bonus increases by " + std::to_string(rule->learning.percentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->learning.maximumPercent)
		+ "% of that contribution. Fractions round down; other experience modifiers are unchanged. ";
	if(archivist)
	{
		const bool eagleEye = newHorizonsMagic::rulesActive(magic)
			&& magic["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION;
		result.description += eagleEye ? "While Eagle Eye is active, learn up to "
			+ std::to_string(rule->additionalEagleEyeSpells)
			+ " additional distinct eligible enemy-hero spells after combat. Highest level first; equal-level ties use cast order. Ordinary learning restrictions apply."
			: "Eagle Eye's additional learning is inactive under this game's legacy magic rules.";
	}
	else
		result.description += "When ordinary Mentor or Master Teacher authorizes teaching, its raw experience award increases by "
			+ std::to_string(rule->mentorExperiencePercent)
			+ "% before the recipient's ordinary experience modifiers. This grants no perk or extra weekly recipient.";
	if(capturedHero && capturedHero->getSecSkillLevel(*result.skill) == MasteryLevel::NONE)
		result.description += " Learning is not currently learned.";
	return true;
}
}
