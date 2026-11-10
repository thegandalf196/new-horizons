/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "BattleSide.h"
#include <array>
#include <cstdint>
#include <optional>

class JsonNode;
class CGHeroInstance;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct HeroOrderState;
struct DamageAttackInfo;
enum class HeroCommand : int8_t;
namespace battle { class Unit; }

namespace newHorizonsWarfare
{
inline constexpr char SKILL[] = "new-horizons:command";
struct DLL_LINKAGE Policy
{
	std::array<int, 4> rankEfficiencyPercent;
	int aggressiveEfficiencyBonusPercent;
	int defensiveEfficiencyBonusPercent;
	int battlePlanEfficiencyBonusPercent;
	int veteranCommanderStats;
	int masterCommanderEfficiencyBonusPercent;
	int decisiveOrdersDamagePercent;
	int unbrokenRanksReductionPercent;
	int combinedArmsFocusFirePercent;
};
DLL_LINKAGE bool usesRules(const JsonNode & combatRules);
DLL_LINKAGE bool enabled(const JsonNode & perkRules);
DLL_LINKAGE bool enabled(const CGHeroInstance * hero);
DLL_LINKAGE void validatePolicy(const JsonNode & perkRules);
DLL_LINKAGE void validateRulesSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<Policy> policy(const CGHeroInstance * hero);
DLL_LINKAGE int perkValue(const CGHeroInstance & hero, const char * perkId, const char * parameter);
DLL_LINKAGE int battlePlanBonus(const CBattleInfoCallback & battle, BattleSide side);
DLL_LINKAGE int statBonus(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE int directAttackBonus(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int incomingReduction(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE int64_t reduceDamage(int64_t damage, int reductionPercent);
DLL_LINKAGE void appendAttackModifiers(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	DamageAttackInfo & payload);
}
