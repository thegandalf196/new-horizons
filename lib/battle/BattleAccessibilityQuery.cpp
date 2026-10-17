/*
 * BattleAccessibilityQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleAccessibilityQuery.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../CCreatureHandler.h"

std::span<const std::pair<EWallPart, BattleHex>> battle::accessibilityWallParts()
{
	static const std::pair<EWallPart, BattleHex> parts[] =
	{
		std::make_pair(EWallPart::BOTTOM_WALL, BattleHex(BattleHex::DESTRUCTIBLE_WALL_4)),
		std::make_pair(EWallPart::BELOW_GATE, BattleHex(BattleHex::DESTRUCTIBLE_WALL_3)),
		std::make_pair(EWallPart::OVER_GATE, BattleHex(BattleHex::DESTRUCTIBLE_WALL_2)),
		std::make_pair(EWallPart::UPPER_WALL, BattleHex(BattleHex::DESTRUCTIBLE_WALL_1))
	};
	return parts;
}

namespace
{
void fillBase(AccessibilityInfo & ret, const battle::BattleAccessibilityContext & context)
{
	ret.fill(EAccessibility::ACCESSIBLE);
	for(int y = 0; y < GameConstants::BFIELD_HEIGHT; y++)
	{
		ret[BattleHex(GameConstants::BFIELD_WIDTH - 1, y).toInt()] = EAccessibility::SIDE_COLUMN;
		ret[BattleHex(0, y).toInt()] = EAccessibility::SIDE_COLUMN;
	}
	for(const auto & hex : context.impassableTiles())
		ret[hex.toInt()] = EAccessibility::UNAVAILABLE;
	// The gate must be applied before living stacks, obstacles and walls.
	if(context.hasWalls())
	{
		EAccessibility accessibility = EAccessibility::ACCESSIBLE;
		switch(context.gateState())
		{
		case EGateState::CLOSED:
			accessibility = EAccessibility::GATE;
			break;
		case EGateState::BLOCKED:
			accessibility = EAccessibility::UNAVAILABLE;
			break;
		}
		ret[BattleHex::GATE_OUTER] = ret[BattleHex::GATE_INNER] = accessibility;
	}
}

void fillWalls(AccessibilityInfo & ret, const battle::BattleAccessibilityContext & context)
{
	if(context.hasWalls())
	{
		static const int permanentlyLocked[] = {12, 45, 62, 112, 147, 165};
		for(const auto & hex : permanentlyLocked)
			ret[hex] = EAccessibility::UNAVAILABLE;
		for(const auto & elem : battle::accessibilityWallParts())
		{
			if(context.wallState(elem.first) != EWallState::DESTROYED)
				ret[elem.second.toInt()] = EAccessibility::DESTRUCTIBLE_WALL;
		}
	}
}

void fillReservations(AccessibilityInfo & ret, const battle::BattleAccessibilityContext & context)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		for(const auto & gate : context.pendingGates(side))
		{
			if(!gate.creature.hasValue())
				continue;
			const auto * creature = gate.creature.toCreature();
			if(creature)
				ret.reserveDemonicGateFootprint(gate.position, creature->isDoubleWide(), side);
		}
	}
}
}

AccessibilityInfo battle::buildBattleAccessibility(const BattleAccessibilityContext & context)
{
	AccessibilityInfo ret;
	fillBase(ret, context);
	for(const auto * unit : context.aliveUnits())
	{
		for(const auto & hex : unit->getHexes())
			if(hex.isAvailable())
				ret[hex.toInt()] = EAccessibility::ALIVE_STACK;
	}
	for(const auto & obstacle : context.blockedObstacles())
	{
		for(const auto & hex : obstacle)
			ret[hex.toInt()] = EAccessibility::OBSTACLE;
	}
	fillWalls(ret, context);
	fillReservations(ret, context);
	return ret;
}

AccessibilityInfo battle::withAccessibleHexes(AccessibilityInfo accessibility, const BattleHexArray & hexes)
{
	for(const auto & hex : hexes)
		if(hex.isValid() && !accessibility.isDemonicGateReserved(hex))
			accessibility[hex.toInt()] = EAccessibility::ACCESSIBLE;
	return accessibility;
}
