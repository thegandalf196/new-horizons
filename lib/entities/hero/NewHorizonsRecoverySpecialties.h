/*
 * NewHorizonsRecoverySpecialties.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <optional>
class JsonNode;
class CHero;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
enum class RecoverySpecialtyKind { INFERNAL_WELLSPRING, RITUAL_RESERVE, DEEP_TRANCE };
struct DLL_LINKAGE RecoverySpecialtyRules
{
	int dailyPercentPerLevel = 0;
	int dailyMaximumPercent = 0;
	int victoryPercent = 0;
	int lowNormalPercent = 0;
	int firstCombatPercent = 0;
};
DLL_LINKAGE void validateRecoverySpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<RecoverySpecialtyRules> recoverySpecialtyRules(
	const JsonNode & development, RecoverySpecialtyKind kind);
DLL_LINKAGE std::optional<RecoverySpecialtyRules> recoverySpecialtyRules(const CGHeroInstance & hero);
DLL_LINKAGE bool hasCapturedDeepTrance(const CGHeroInstance & hero);
DLL_LINKAGE int64_t originalMysticismRecovery(const CGHeroInstance & hero);
DLL_LINKAGE int64_t enhancedMysticismRecovery(const CGHeroInstance & hero, bool completedDay);
DLL_LINKAGE int64_t firstCombatMysticismRecovery(const CGHeroInstance & hero);
DLL_LINKAGE int64_t victoryMysticismRecovery(const CGHeroInstance & hero);
DLL_LINKAGE bool resolveRecoverySpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
