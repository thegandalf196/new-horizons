/*
 * PrimaryFortuneReceipt.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HypotheticBattleTypes.h"
#include "../../lib/battle/BattleSide.h"
#include <optional>

class HypotheticBattle;
struct BattleAttackInfo;

/// One applied primary strike's pre-hit facts; never reused across strikes.
struct PrimaryFortuneReceipt
{
	bool perfectFortune = false;
	BattleSide controllerSide = BattleSide::NONE;
	bool ordinaryLuckAttack = false;
	ProjectedLuckOutcome resolvedLuck = ProjectedLuckOutcome::UNKNOWN;
	bool operator==(const PrimaryFortuneReceipt &) const = default;
};

/// Target probes have no primary strike aftermath. Applied calls retain the
/// original four reads in order, before any damage or bonus/state consumption.
std::optional<PrimaryFortuneReceipt> captureAppliedFortuneReceipt(
	const HypotheticBattle & battle, const BattleAttackInfo & attack, bool evaluateOnly);
