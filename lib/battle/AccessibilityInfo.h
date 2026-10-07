/*
 * AccessibilityInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "BattleHex.h"
#include "BattleHexArray.h"
#include "../GameConstants.h"
#include <optional>

namespace battle
{
	class Unit;
}

//Accessibility is property of hex in battle. It doesn't depend on stack, side's perspective and so on.
enum class EAccessibility
{
	ACCESSIBLE,
	ALIVE_STACK,
	OBSTACLE,
	DESTRUCTIBLE_WALL,
	GATE, //sieges -> gate opens only for defender stacks
	UNAVAILABLE, //indestructible wall parts, special battlefields (like boat-to-boat)
	SIDE_COLUMN, //used for first and last columns of hexes that are unavailable but war machines can stand there
	DEMONIC_GATE_RESERVED // pending Demonic Gate arrival footprint
};


using TAccessibilityArray = std::array<EAccessibility, GameConstants::BFIELD_SIZE>;
using TBattlefieldTurnsArray = std::array<int8_t, GameConstants::BFIELD_SIZE>;

struct DLL_LINKAGE AccessibilityInfo : TAccessibilityArray
{
	std::shared_ptr<const TBattlefieldTurnsArray> destructibleEnemyTurns; //used only as a view for destructibleEnemyTurns from ReachabilityInfo::Parameters
	std::array<uint32_t, GameConstants::BFIELD_SIZE> demonicGateReservationCounts{};
	TAccessibilityArray demonicGateReservationBase{};

	public:
		bool accessible(const BattleHex & tile, const battle::Unit * stack) const; //checks for both tiles if stack is double wide
		bool accessible(const BattleHex & tile, bool doubleWide, BattleSide side) const; //checks for both tiles if stack is double wide
		/// Pass-through movement may traverse these blocked cells, but may not end on them.
		bool accessibleForPassThroughTransit(const BattleHex & tile, bool doubleWide, BattleSide side) const;
		/// Normal movement endpoint validation, ignoring hypothetical destructible-enemy forecasts.
		bool accessibleForMovementEndpoint(const BattleHex & tile, bool doubleWide, BattleSide side) const;
		/// True when at least one shortest straight hex ray has no blocking intermediate cell.
		/// Occupied endpoint-footprint cells may be listed in allowedOccupiedHexes.
		bool hasClearStraightHexRay(const BattleHex & from, const BattleHex & to,
			const BattleHexArray & allowedOccupiedHexes = {}) const;
		/// Magical relocation, not walking/pathfinding. Equal-distance anchors use
		/// stable battlefield order; an entirely blocked field returns no position.
		std::optional<BattleHex> nearestLegalPosition(const BattleHex & origin, bool doubleWide, BattleSide side) const;
		bool accessibleForDemonicGateArrival(const BattleHex & tile, bool doubleWide, BattleSide side,
			const BattleHex & reservedPosition, bool reservedDoubleWide) const;
		void reserveDemonicGateFootprint(const BattleHex & position, bool doubleWide, BattleSide side);
		bool isDemonicGateReserved(const BattleHex & tile) const;
	private:
		bool tileAccessibleWithGate(const BattleHex & tile, BattleSide side, uint32_t ignoredGateReservations = 0,
			bool allowDestructibleEnemyTurns = true) const;
		bool accessibleImpl(const BattleHex & tile, bool doubleWide, BattleSide side,
		const BattleHex & ignoredReservationPosition, bool ignoredReservationDoubleWide,
			bool allowDestructibleEnemyTurns = true) const;
};
