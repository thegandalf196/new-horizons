/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "Unit.h"

/// Pure gate geometry. The caller owns the existing single wide-moat query.
inline bool gateDrawbridgeFootprintContains(const battle::Unit & unit,
	const BattleHex & hex, bool hasWideMoat)
{
	const auto isGateHex = [hasWideMoat](const BattleHex & position)
	{
		if(hasWideMoat && position == BattleHex::GATE_BRIDGE)
			return true;
		if(position == BattleHex::GATE_OUTER)
			return true;
		if(position == BattleHex::GATE_INNER)
			return true;
		return false;
	};
	if(isGateHex(hex))
		return true;
	if(unit.doubleWide())
	{
		const BattleHex otherHex = unit.occupiedHex(hex);
		if(otherHex.isValid() && isGateHex(otherHex))
			return true;
	}
	return false;
}
