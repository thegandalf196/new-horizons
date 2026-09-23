/*
 * AccessibilityInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AccessibilityInfo.h"
#include "BattleHex.h"
#include "Unit.h"
#include "../GameConstants.h"

#include <limits>

bool AccessibilityInfo::tileAccessibleWithGate(const BattleHex & tile, BattleSide side, uint32_t ignoredGateReservations) const
{
	if(!tile.isValid())
		return false;

	const auto index = static_cast<size_t>(tile.toInt());
	if(demonicGateReservationCounts[index] > ignoredGateReservations)
		return false;

	auto accessibility = at(index);
	if(accessibility == EAccessibility::DEMONIC_GATE_RESERVED && ignoredGateReservations > 0)
		accessibility = demonicGateReservationBase[index];

	if(accessibility == EAccessibility::ALIVE_STACK)
	{
		if(!destructibleEnemyTurns)
			return false;

		return destructibleEnemyTurns->at(tile.toInt()) >= 0;
	}

	if(accessibility != EAccessibility::ACCESSIBLE)
		if(accessibility != EAccessibility::GATE || side != BattleSide::DEFENDER)
			return false;

	return true;
}

bool AccessibilityInfo::accessible(const BattleHex & tile, const battle::Unit * stack) const
{
	return accessible(tile, stack->doubleWide(), stack->unitSide());
}

bool AccessibilityInfo::accessible(const BattleHex & tile, bool doubleWide, BattleSide side) const
{
	return accessibleImpl(tile, doubleWide, side, BattleHex::INVALID, false);
}

bool AccessibilityInfo::accessibleForDemonicGateArrival(const BattleHex & tile, bool doubleWide, BattleSide side,
	const BattleHex & reservedPosition, bool reservedDoubleWide) const
{
	return accessibleImpl(tile, doubleWide, side, reservedPosition, reservedDoubleWide);
}

bool AccessibilityInfo::accessibleImpl(const BattleHex & tile, bool doubleWide, BattleSide side,
	const BattleHex & ignoredReservationPosition, bool ignoredReservationDoubleWide) const
{
	// All hexes that stack would cover if standing on tile have to be accessible.
	//do not use getHexes for speed reasons
	if(!tile.isValid())
		return false;

	const BattleHex ignoredReservationOtherHex = ignoredReservationPosition.isValid()
		? battle::Unit::occupiedHex(ignoredReservationPosition, ignoredReservationDoubleWide, side)
		: BattleHex::INVALID;
	auto ignoredReservationCount = [this, &ignoredReservationPosition, &ignoredReservationOtherHex](const BattleHex & hex)
	{
		const bool ownFootprint = hex == ignoredReservationPosition || hex == ignoredReservationOtherHex;
		return ownFootprint && isDemonicGateReserved(hex) ? 1u : 0u;
	};

	if(!tileAccessibleWithGate(tile, side, ignoredReservationCount(tile)))
		return false;

	if(doubleWide)
	{
		auto otherHex = battle::Unit::occupiedHex(tile, doubleWide, side);
		if(!otherHex.isValid())
			return false;
		if(!tileAccessibleWithGate(otherHex, side, ignoredReservationCount(otherHex)))
			return false;
	}

	return true;
}

void AccessibilityInfo::reserveDemonicGateFootprint(const BattleHex & position, bool doubleWide, BattleSide side)
{
	if(!position.isAvailable())
		return;

	for(const auto & hex : battle::Unit::getHexes(position, doubleWide, side))
	{
		if(!hex.isAvailable())
			continue;

		const auto index = static_cast<size_t>(hex.toInt());
		if(demonicGateReservationCounts[index] == 0)
		{
			demonicGateReservationBase[index] = at(index);
			if(at(index) == EAccessibility::ACCESSIBLE || at(index) == EAccessibility::GATE)
				at(index) = EAccessibility::DEMONIC_GATE_RESERVED;
		}
		if(demonicGateReservationCounts[index] < std::numeric_limits<uint32_t>::max())
			++demonicGateReservationCounts[index];
	}
}

bool AccessibilityInfo::isDemonicGateReserved(const BattleHex & tile) const
{
	return tile.isAvailable() && demonicGateReservationCounts[static_cast<size_t>(tile.toInt())] > 0;
}
