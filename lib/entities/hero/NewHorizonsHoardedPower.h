/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <optional>
#include <cstdint>
class JsonNode;
class CGHeroInstance;
struct SideInBattle;
namespace newHorizonsHeroes
{
struct HoardedPowerRules
{
	int baseCapPercent = 0;
	int capPercentPerLevel = 0;
	int maximumCapPercent = 0;
};
DLL_LINKAGE std::optional<HoardedPowerRules> hoardedPowerRules(const JsonNode & development);
DLL_LINKAGE std::optional<int32_t> hoardedPowerCap(const CGHeroInstance & hero);
DLL_LINKAGE int32_t hoardedPowerCap(const HoardedPowerRules & rules, int level, int32_t maximumMana);
/// Fixed recovery fills Normal, then stores the wasted share subject to Nagash's cap.
DLL_LINKAGE bool restoreFixedSpellPoints(CGHeroInstance & hero, int32_t amount);
DLL_LINKAGE void reconcileHoardedPowerBuffer(CGHeroInstance & hero, SideInBattle * side);
DLL_LINKAGE int64_t rewardBufferGain(const CGHeroInstance & hero, int64_t normalBeforeRecovery,
	int32_t fixedRecovery, int32_t explicitGrant, int64_t bufferSpent);
DLL_LINKAGE int32_t dailyHoardedPower(const CGHeroInstance & hero, bool completedDay);
DLL_LINKAGE int32_t blackHarvestRecoveryLimit(const CGHeroInstance & hero, int32_t temporaryBufferRemaining = 0);
}
