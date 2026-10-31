/*
 * BattleExchangeAftermath.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <optional>
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include <cstdint>
class HypotheticBattle;
class CBattleInfoCallback;
class StackWithBonuses;
struct FortuneStrikeProjection;
#include <vector>
#include <utility>
struct BattleAttackInfo;
struct BattleScore;
namespace battleExchangeAftermath
{
/// Existing post-exchange horizon/turn scoring, after all round/lifecycle work.
BattleScore finalizeExchangeScore(BattleScore score, int simulationTurnsCount, int totalTurnsCount, int turn);
void replayPrimaryStatuses(HypotheticBattle & battle, const BattleAttackInfo & attack,
	const FortuneStrikeProjection & strike, const std::vector<std::pair<uint32_t, int64_t>> & actualHits,
	bool binding, bool gaze, int64_t primaryBodyHealthLoss);
void applyPrimaryStatuses(HypotheticBattle & battle, const BattleAttackInfo & attack,
	int64_t primaryDamage, bool binding, bool gaze, int64_t primaryBodyHealthLoss = 0,
	const newHorizonsOrdinaryStrike::EffectPlan & lanceFormation = newHorizonsOrdinaryStrike::EffectPlan{});
bool projectNoQuarterAfterHit(HypotheticBattle & battle, const BattleAttackInfo & attack,
	StackWithBonuses & target);
std::optional<uint32_t> relentlessAssaultPrimaryTargetId(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack);
void projectRelentlessAssaultAttack(HypotheticBattle & battle, const BattleAttackInfo & attack,
	std::optional<uint32_t> primaryTargetUnitId);
}
