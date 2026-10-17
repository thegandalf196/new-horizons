/*
 * ProjectedBattleAccessibility.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <array>
#include "BattleAccessibilityQuery.h"
#include "IBattleState.h"

class ProjectedBonusGraph;
class ProjectedBonusRoster;
class CBattleInfoCallback;

namespace battle
{
/// Owned nonunit inputs, not an independent simulation or saved format. Future
/// ordered world transitions update these same captured values explicitly.
struct DLL_LINKAGE BattleAccessibilityState
{
	BattleHexArray impassable;
	bool walls = false;
	EGateState gate = EGateState::NONE;
	std::array<EWallState, static_cast<size_t>(EWallPart::PARTS_COUNT)> wallStates;
	std::vector<BattleHexArray> obstacles;
	BattleSideArray<std::vector<PendingDemonicGateFootprint>> reservations;
	BattleAccessibilityState();
	/// Refuse a hidden player view or absent battle before metadata collection.
	/// Source/libraries are stable under the caller's real ownership interval.
	static BattleAccessibilityState capture(const CBattleInfoCallback & source);
};

/// Concrete latest-roster adapter; no ordinary detached/live fallback. Caller
/// owns complete roster, exact graph-bound units and state throughout queries.
/// No lease, common commit, event replay or UPDATE route is supplied here.
class DLL_LINKAGE ProjectedBattleAccessibilityContext final : public BattleAccessibilityContext
{
	const BattleAccessibilityState & state;
	const ProjectedBonusRoster & roster;
	const ProjectedBonusGraph & graph;
public:
	ProjectedBattleAccessibilityContext(const BattleAccessibilityState & state,
		const ProjectedBonusRoster & roster, const ProjectedBonusGraph & graph);
	BattleHexArray impassableTiles() const override;
	bool hasWalls() const override;
	EGateState gateState() const override;
	Units aliveUnits() const override;
	std::vector<BattleHexArray> blockedObstacles() const override;
	EWallState wallState(EWallPart part) const override;
	std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const override;
};
}
