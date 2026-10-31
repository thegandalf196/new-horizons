/* Part of VCMI / New Horizons. GPL-2.0-or-later; authors listed in AUTHORS. */
#pragma once
#include <cstdint>
#include <memory>
class AttackPossibility;
class CBattleInfoCallback;
class DamageCache;
namespace battle { class CUnitState; }
namespace physicalPoisonValue
{
/// Existing deterministic Poison opportunity horizon, without advancing a unit.
int64_t projectedDamage(const battle::CUnitState * state);
/// Existing deterministic Toxic Spines application and residual-value owner.
void applySpines(AttackPossibility & preview, battle::CUnitState * reflectedFrom,
	int64_t actualReflectedDamage, bool ranged, int32_t currentRound,
	const std::shared_ptr<CBattleInfoCallback> & battle, DamageCache & damageCache);
}
