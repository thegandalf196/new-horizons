/*
 * NewHorizonsSwampSeer.h, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <optional>
class JsonNode;
class CGHeroInstance;
class CHero;
namespace Rewardable { struct Reward; }
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
struct DLL_LINKAGE SwampSeerRules
{
	int learningPercentPerLevel = 0;
	int learningMaximumPercent = 0;
	int studyExperiencePerMagicPrimaryPoint = 0;
};
struct DLL_LINKAGE StudyPrimarySnapshot
{
	int32_t knowledge = 0;
	int32_t spellPower = 0;
};
DLL_LINKAGE void validateSwampSeerSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<SwampSeerRules> swampSeerRules(const JsonNode & development);
DLL_LINKAGE std::optional<SwampSeerRules> swampSeerRules(const CGHeroInstance * hero);
DLL_LINKAGE bool isSwampSeerStudyObject(std::optional<Obj> source);
/// Snapshot the exact permanent base fields the ordinary primary setter mutates.
DLL_LINKAGE std::optional<StudyPrimarySnapshot> captureStudyPrimarySnapshot(
	const CGHeroInstance & hero, std::optional<Obj> source);
DLL_LINKAGE int64_t acceptedStudyPrimaryPoints(const CGHeroInstance & hero,
	const std::optional<StudyPrimarySnapshot> & before);
DLL_LINKAGE int64_t projectedStudyPrimaryPoints(const CGHeroInstance & hero,
	const Rewardable::Reward & reward, std::optional<Obj> source);
DLL_LINKAGE int64_t swampSeerStudyExperience(const CGHeroInstance * hero,
	std::optional<Obj> source, int64_t acceptedPrimaryPoints);
DLL_LINKAGE int64_t projectedSwampSeerStudyExperience(const CGHeroInstance * hero,
	const Rewardable::Reward & reward, std::optional<Obj> source);
DLL_LINKAGE int64_t addStudyExperience(int64_t ordinary, int64_t study);
DLL_LINKAGE bool resolveSwampSeerPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
