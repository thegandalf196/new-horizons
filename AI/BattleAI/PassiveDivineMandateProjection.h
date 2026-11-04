/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "PassiveDivineMandateValue.h"
#include "../../lib/battle/BattleSide.h"
#include <memory>
class HypotheticBattle;
class Environment;
class DamageCache;
struct HeroOrderState;
namespace passiveDivineMandateAI
{
/// Caller supplies the accepted post-Order parent, before any creature
/// continuation. One deterministic native spell projection per random outcome;
/// fixed ordinary attack references are prepared once per Order. No recursive
/// Hero candidate search, combat queue, random draw or authoritative mutation.
std::optional<float> projectedValue(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & postOrder, BattleSide side,
	const HeroOrderState & order, const DamageCache & damage,
	float positiveEffectMultiplier, float negativeEffectMultiplier,
	Workload * workload = nullptr);
}
