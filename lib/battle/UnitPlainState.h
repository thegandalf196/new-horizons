/*
 * UnitPlainState.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "CUnitState.h"

namespace battle
{
/// The bonus-free scalars of one battle unit that limiters, updaters and
/// adjacency rules read: hex, life and count (from the raw health ledger),
/// ghost state and the battle-form view revision (which covers the effective
/// creature, width and form). Read from the public fields themselves, so an
/// edit by any writer, mutator or not, changes the signature.
struct UnitPlainState
{
	BattleHex position;
	int32_t count = 0;
	int32_t firstHitPoints = 0;
	int32_t formRevision = 0;
	bool ghost = false;
	bool operator==(const UnitPlainState &) const = default;
};

inline UnitPlainState plainState(const CUnitState & unit)
{
	return {unit.position, unit.health.getCount(), unit.health.getFirstHPleft(), unit.getBattleFormViewRevision(),
		unit.ghost};
}
}
