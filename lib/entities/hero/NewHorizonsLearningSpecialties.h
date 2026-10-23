/*
 * NewHorizonsLearningSpecialties.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "NewHorizonsHeroRules.h"
namespace newHorizonsHeroes
{
enum class LearningSpecialtyKind { GRAVE_ARCHIVIST, SPIRIT_TEACHER };
struct DLL_LINKAGE LearningSpecialtyRules
{
	LevelScaledSpecialty learning;
	int additionalEagleEyeSpells = 0;
	int mentorExperiencePercent = 0;
};
DLL_LINKAGE void validateLearningSpecialtyRows(const JsonNode & table);
DLL_LINKAGE std::optional<LearningSpecialtyRules> learningSpecialtyRules(
	const JsonNode & development, LearningSpecialtyKind kind);
DLL_LINKAGE std::optional<LearningSpecialtyRules> learningSpecialtyRules(const CGHeroInstance * hero);
DLL_LINKAGE int additionalEagleEyeSpells(const CGHeroInstance * hero);
DLL_LINKAGE int64_t mentorSpecialtyAward(const CGHeroInstance * mentor, int64_t ordinaryAward);
DLL_LINKAGE int64_t calculateLearningSpecialtyExperience(const CGHeroInstance & hero, int64_t exp, int32_t additionalPercent);
DLL_LINKAGE bool resolveLearningSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
