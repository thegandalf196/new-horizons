/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "TemporaryGatingAI.h"
#include <memory>
class Environment;
class HypotheticBattle;
class DamageCache;
namespace temporaryGatingAI
{
/// Compare a bounded single arriving activation plus native arrival effects to
/// the actual source activation spent. Finite horizon supplies the arrival
/// discount; no full battle queue or future Gate/attack sequence is searched.
std::optional<BattleAction> projectedChoice(const Environment *,
	const std::shared_ptr<HypotheticBattle> &, const battle::Unit &, BattleSide,
	const DamageCache &, float ordinaryActivationValue, int forecastRounds,
	Workload * workload = nullptr);
}
