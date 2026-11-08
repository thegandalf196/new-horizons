/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BarrageProbability.h"
#include <optional>
#include <memory>
class AttackPossibility;
class CBattleInfoCallback;
class DamageCache;
namespace barrageAI
{
struct Prepared
{
	long double probability = 0;
	int shots = 0;
	int damagePercent = 0;
};
/// Fixed nominal pre-sequence range; no computation for an unrelated shooter.
std::optional<Prepared> prepare(const AttackPossibility &, int normalShots,
	const std::shared_ptr<CBattleInfoCallback> &);
/// Expected opportunity only: fractional outcomes never change copied health/ammo.
float value(const AttackPossibility &, const Prepared &, DamageCache &,
	const std::shared_ptr<CBattleInfoCallback> &);
}
