/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <memory>
#include <string>
#include <utility>
#include "SylvanLuckState.h"
#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"
class CGHeroInstance;
class JsonNode;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct BattleAttack;
struct BattleStackAttacked;
struct DamageAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsSylvanTactics
{
inline constexpr int PERFECT_MOMENT_TOTAL_PERCENT = 350;
inline constexpr int PATIENT_HUNTER_PERCENT = 15;
inline constexpr int MEASURED_ADVANCE_PERCENT = 10;
inline constexpr int COVERING_FIRE_REDUCTION_PERCENT = 15;
inline constexpr int DAPPLED_COVER_REDUCTION_PERCENT = 20;
inline constexpr int DISRUPTIVE_INITIATIVE_PERCENT = -15;
DLL_LINKAGE std::string capturedSkillName(const JsonNode & rules);
DLL_LINKAGE std::string capturedRankDescription(const JsonNode & rules, int rank);
DLL_LINKAGE std::vector<std::pair<std::string, int>> speedContributions(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE std::vector<std::pair<std::string, int>> rangedProtectionContributions(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE std::vector<int> rangedProtection(const CBattleInfoCallback & battle, const battle::Unit * unit);
struct StrikeSnapshot
{
	bool valid = false;
	bool shooting = false;
	uint32_t shooter = 0;
	std::optional<uint32_t> primary;
	std::set<uint32_t> eligibleTargets;
	std::vector<uint32_t> adjacentFriends;
};
/// Add one pre-hit victim without repeating the strike neighbor snapshot.
DLL_LINKAGE void captureVictim(StrikeSnapshot & snapshot, const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * target);
DLL_LINKAGE StrikeSnapshot captureStrike(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * primary,
	const std::vector<const battle::Unit *> & targets, bool shooting, bool physical);
DLL_LINKAGE void finishStrike(const StrikeSnapshot & snapshot,
	const std::vector<std::pair<uint32_t, int64_t>> & actualHits,
	const std::vector<uint32_t> & destroyedWithoutRebirth, SylvanTacticsState & state);
DLL_LINKAGE void validateStrikeTransition(const CBattleInfoCallback & battle, const BattleAttack & packet);
DLL_LINKAGE bool ordinary(const battle::Unit * unit);
DLL_LINKAGE bool enabled(const CGHeroInstance * hero);
DLL_LINKAGE void configure(const CGHeroInstance & hero, SylvanLuckState & fortune);
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> rankBonuses(const CGHeroInstance & hero,
	SecondarySkill skill, int rank, const std::vector<std::shared_ptr<Bonus>> & current);
/// Snapshot qualified victims/actual neighbors BEFORE applying the hit packet.
DLL_LINKAGE void finishStrike(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * primary, const std::vector<BattleStackAttacked> & hits, bool shooting,
	bool physical, SylvanTacticsState & state);
DLL_LINKAGE void finalizeDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & info,
	DamageAttackInfo & payload);
}
