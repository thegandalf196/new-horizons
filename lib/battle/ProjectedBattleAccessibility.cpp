/*
 * ProjectedBattleAccessibility.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleAccessibility.h"
#include "LiveBattleAccessibilityContext.h"
#include "CBattleInfoCallback.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include <stdexcept>

battle::BattleAccessibilityState::BattleAccessibilityState()
{
	wallStates.fill(EWallState::NONE);
}

battle::BattleAccessibilityState battle::BattleAccessibilityState::capture(const CBattleInfoCallback & source)
{
	if(!source.getBattle() || source.getPlayerID())
		throw std::invalid_argument("Projected accessibility requires an active full-authority battle view");
	const LiveBattleAccessibilityContext live(source);
	BattleAccessibilityState result;
	result.impassable = live.impassableTiles();
	result.walls = live.hasWalls();
	if(result.walls)
		result.gate = live.gateState();
	result.obstacles = live.blockedObstacles();
	if(result.walls)
		for(const auto & part : accessibilityWallParts())
			result.wallStates.at(static_cast<size_t>(part.first)) = live.wallState(part.first);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		result.reservations.at(side) = live.pendingGates(side);
	return result;
}

battle::ProjectedBattleAccessibilityContext::ProjectedBattleAccessibilityContext(const BattleAccessibilityState & state,
	const ProjectedBonusRoster & roster, const ProjectedBonusGraph & graph)
	: state(state), roster(roster), graph(graph)
{
}

BattleHexArray battle::ProjectedBattleAccessibilityContext::impassableTiles() const { return state.impassable; }
bool battle::ProjectedBattleAccessibilityContext::hasWalls() const { return state.walls; }
EGateState battle::ProjectedBattleAccessibilityContext::gateState() const { return state.gate; }
EWallState battle::ProjectedBattleAccessibilityContext::wallState(EWallPart part) const
{
	return state.wallStates.at(static_cast<size_t>(part));
}
std::vector<BattleHexArray> battle::ProjectedBattleAccessibilityContext::blockedObstacles() const { return state.obstacles; }
std::vector<PendingDemonicGateFootprint> battle::ProjectedBattleAccessibilityContext::pendingGates(BattleSide side) const
{
	return state.reservations.at(side);
}

battle::Units battle::ProjectedBattleAccessibilityContext::aliveUnits() const
{
	// Validate the complete roster before any provider-dependent target query.
	for(const auto * unit : roster.orderedUnits())
		graph.requireBoundUnit(*unit);
	Units result;
	for(const auto * unit : roster.orderedUnits())
		if(unit->isValidTarget(false))
			result.push_back(unit);
	return result;
}
