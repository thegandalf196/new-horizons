/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include <cstdint>
#include <memory>
class DamageCache;
class HypotheticBattle;
namespace elementalAttunementAI
{
/// Only for accepted terminal spell heuristics that skip the generic exchange.
/// Restores just the pre-cast mask on an isolated copy of the same post-cast board.
/// Repeated/untagged/countered casts return before copying or damage evaluation.
float terminalMarginal(const std::shared_ptr<HypotheticBattle> & accepted,
	BattleSide side, uint8_t previousMask, const DamageCache & originalDamage,
	float positiveMultiplier = 1.0f, float negativeMultiplier = 1.0f);
}
