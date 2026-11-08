/*
 * BattleMovementCompilationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../Global.h"
#include "MovementLegacyReference.h"
#include <iostream>
#include <random>
#include <stdexcept>

namespace
{
using Costs = std::array<std::int32_t, GameConstants::BFIELD_SIZE>;

void require(bool value, const char * message)
{
	if(!value)
		throw std::runtime_error(message);
}

AccessibilityInfo openField()
{
	AccessibilityInfo result;
	result.fill(EAccessibility::ACCESSIBLE);
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
		if(!BattleHex(i).isAvailable())
			result[i] = EAccessibility::SIDE_COLUMN;
	return result;
}

void compare(const AccessibilityInfo & accessibility, const ReachabilityInfo::Parameters & params,
	const BattleHexArray & stoppers, EGateState gate, const Costs & costs)
{
	const auto query = params.flying ? battle::compileFlyingMovementQuery(accessibility, params)
		: battle::compileWalkingMovementQuery(accessibility, params, stoppers, gate, costs);
	require(battle::solveMovementDirect(accessibility, params, stoppers, gate, costs)
		== movementLegacy::solve(accessibility, params, stoppers, gate, costs), "Native fallback changed paths");
	const auto actual = battle::solveMovementQuery(query);
	require(actual == movementLegacy::solve(accessibility, params, stoppers, gate, costs),
		"Native movement compilation differs from the original paths or tie choices");
	if(params.flying)
	{
		require(battle::solveFlyingMovementDirect(accessibility, params) == actual,
			"Dedicated flying wrapper changed paths");
		ReachabilityInfo inPlace;
		battle::fillFlyingMovementPaths(accessibility, params, inPlace.distances, inPlace.predecessors);
		require(inPlace.distances == actual.distances, "In-place flying fill changed distances");
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			require(inPlace.predecessors[hex].toInt() == actual.predecessors[hex],
				"In-place flying fill changed predecessors");
	}
}

void gateKnownFootprintAndReservations()
{
	auto access = openField();
	BattleHexArray known{BattleHex(8, 5)};
	const BattleHexArray stoppers{BattleHex(BattleHex::GATE_BRIDGE), BattleHex(8, 5)};
	ReachabilityInfo::Parameters params;
	params.startPosition = BattleHex(8, 5);
	params.knownAccessible = &known;
	params.side = BattleSide::ATTACKER;
	Costs costs{};
	auto closed = battle::compileWalkingMovementQuery(access, params, stoppers, EGateState::CLOSED, costs);
	require(closed.stoppers[BattleHex::GATE_BRIDGE], "Closed bridge must stop attackers");
	require(!closed.stoppers[params.startPosition.toInt()], "Starting footprint must permit departure");
	auto destroyed = battle::compileWalkingMovementQuery(access, params, stoppers, EGateState::DESTROYED, costs);
	require(!destroyed.stoppers[BattleHex::GATE_BRIDGE], "Destroyed bridge must not stop movement");
	params.side = BattleSide::DEFENDER;
	auto defending = battle::compileWalkingMovementQuery(access, params, stoppers, EGateState::CLOSED, costs);
	require(!defending.stoppers[BattleHex::GATE_BRIDGE], "Bridge exception must retain physical side");
	params.doubleWide = true;
	access.reserveDemonicGateFootprint(BattleHex(10, 5), true, BattleSide::ATTACKER);
	for(const auto gate : {EGateState::CLOSED, EGateState::OPENED, EGateState::DESTROYED})
		compare(access, params, stoppers, gate, costs);
	const auto reserved = battle::compileWalkingMovementQuery(access, params, stoppers, EGateState::CLOSED, costs);
	require(!reserved.traversable[BattleHex(10, 5).toInt()], "Reserved landing cannot become a path cell");
}

void transitAndIndependentInputLifetime()
{
	auto access = openField();
	BattleHexArray known{BattleHex(5, 5)};
	ReachabilityInfo::Parameters params;
	params.startPosition = BattleHex(5, 5);
	params.knownAccessible = &known;
	params.side = BattleSide::ATTACKER;
	const BattleHex friendly(6, 5), hostile(7, 5), blocked(8, 5);
	access[friendly.toInt()] = access[hostile.toInt()] = EAccessibility::ALIVE_STACK;
	access[blocked.toInt()] = EAccessibility::OBSTACLE;
	params.friendlyTransit.set(friendly.toInt());
	Costs costs{};
	const auto passing = battle::compileWalkingMovementQuery(access, params, {}, EGateState::NONE, costs);
	require(passing.traversable[friendly.toInt()] && !passing.traversable[hostile.toInt()], "Passing Lines leaked to enemy cells");
	params.ghostWalk = true;
	const auto ghost = battle::compileWalkingMovementQuery(access, params, {}, EGateState::NONE, costs);
	require(ghost.traversable[hostile.toInt()] && !ghost.traversable[blocked.toInt()], "Ghost Walk changed noncreature blockers");
	params.passThrough = true;
	const auto through = battle::compileWalkingMovementQuery(access, params, {}, EGateState::NONE, costs);
	require(through.traversable[blocked.toInt()], "Pass-through lost obstacle transit");
	const auto retained = through;
	known.clear();
	access.fill(EAccessibility::OBSTACLE);
	costs.fill(29);
	params.friendlyTransit.reset();
	require(retained == through, "Compiled input borrowed mutable source storage");
	require(through.traversable[blocked.toInt()] && through.movementCost[blocked.toInt()] == 0,
		"Source mutation changed the owned query");
}

void exhaustiveStoppingFootprints()
{
	const auto access = openField();
	const Costs costs{};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		for(const bool wide : {false, true})
			for(const auto gate : {EGateState::CLOSED, EGateState::DESTROYED})
				for(int obstacle = 0; obstacle < GameConstants::BFIELD_SIZE; ++obstacle)
				{
					ReachabilityInfo::Parameters params;
					params.startPosition = BattleHex(5, 5);
					params.side = side;
					params.doubleWide = wide;
					BattleHexArray known{BattleHex(6, 5)};
					params.knownAccessible = &known;
					const BattleHexArray stoppers{BattleHex(obstacle)};
					const auto query = battle::compileWalkingMovementQuery(access, params, stoppers, gate, costs);
					for(int anchor = 0; anchor < GameConstants::BFIELD_SIZE; ++anchor)
					{
						bool expected = false;
						for(const auto hex : battle::Unit::getHexes(anchor, wide, side))
							if(!known.contains(hex) && stoppers.contains(hex)
								&& (hex != BattleHex::GATE_BRIDGE || (gate != EGateState::DESTROYED && side == BattleSide::ATTACKER)))
								expected = true;
						require(query.stoppers[anchor] == expected, "Inverted stopper footprint changed an anchor");
					}
				}
}

void seededNativeParity()
{
	std::mt19937 random(0x689122u);
	for(int sample = 0; sample < 720; ++sample)
	{
		auto access = openField();
		ReachabilityInfo::Parameters params;
		params.side = sample % 2 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		params.doubleWide = sample % 3 == 0;
		params.flying = sample % 11 == 0;
		params.passThrough = sample % 5 == 0;
		params.ghostWalk = sample % 7 == 0;
		params.bypassEnemyStacks = sample % 4 == 0;
		params.startPosition = sample % 41 ? BattleHex(3 + random() % 10, 2 + random() % 7) : BattleHex(-2);
		BattleHexArray known = battle::Unit::getHexes(params.startPosition, params.doubleWide, params.side);
		params.knownAccessible = &known;
		BattleHexArray stoppers;
		Costs costs{};
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		{
			if(BattleHex(hex).isAvailable())
			{
				const auto roll = random() % 10;
				if(roll < 3) access[hex] = EAccessibility::ALIVE_STACK;
				else if(roll == 3) access[hex] = EAccessibility::OBSTACLE;
				else if(roll == 4) access[hex] = EAccessibility::GATE;
				if(random() % 37 == 0) access.reserveDemonicGateFootprint(hex, false, params.side);
			}
			params.friendlyTransit.set(hex, random() % 7 == 0);
			params.destructibleEnemyTurns[hex] = random() % 4 ? -1 : random() % 15;
			costs[hex] = sample % 3 ? random() % 6 : 0;
			if(random() % 17 == 0) stoppers.insert(hex);
		}
		for(const auto hex : known)
			if(!access.isDemonicGateReserved(hex)) access[hex.toInt()] = EAccessibility::ACCESSIBLE;
		access.destructibleEnemyTurns = std::make_shared<const TBattlefieldTurnsArray>(params.destructibleEnemyTurns);
		const auto gate = static_cast<EGateState>(sample % 5);
		compare(access, params, stoppers, gate, costs);
	}
}
}

int main()
{
	try
	{
		gateKnownFootprintAndReservations();
		transitAndIndependentInputLifetime();
		exhaustiveStoppingFootprints();
		seededNativeParity();
		std::cout << "Battle movement compilation: feature cases and 720 native reference comparisons passed\n";
		return EXIT_SUCCESS;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
