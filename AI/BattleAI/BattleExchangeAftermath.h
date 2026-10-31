/*
 * BattleExchangeAftermath.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <optional>
#include <cstdint>
class HypotheticBattle;
class CBattleInfoCallback;
class StackWithBonuses;
struct BattleAttackInfo;
namespace battleExchangeAftermath
{
void applyPrimaryStatuses(HypotheticBattle & battle, const BattleAttackInfo & attack,
	int64_t primaryDamage, bool binding, bool gaze);
bool projectNoQuarterAfterHit(HypotheticBattle & battle, const BattleAttackInfo & attack,
	StackWithBonuses & target);
std::optional<uint32_t> relentlessAssaultPrimaryTargetId(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack);
void projectRelentlessAssaultAttack(HypotheticBattle & battle, const BattleAttackInfo & attack,
	std::optional<uint32_t> primaryTargetUnitId);
}
