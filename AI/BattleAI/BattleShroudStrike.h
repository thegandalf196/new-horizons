/*
 * BattleShroudStrike.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include <optional>

class CBattleInfoCallback;
struct BattleAttackInfo;

/// Value-only facts for one strike, before damage or bonus mutations. Never retain across strikes.
struct BattleShroudStrike
{
	bool evasiveShroud = false;
	bool ambusher = false;
	std::optional<BattleSide> shadowAssaultSide;

	bool operator==(const BattleShroudStrike &) const = default;
};

BattleShroudStrike evaluateBattleShroudStrike(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack);

/// Selected replay retains its original single-purpose query and evaluation order.
std::optional<BattleSide> qualifyingShadowAssaultSide(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
