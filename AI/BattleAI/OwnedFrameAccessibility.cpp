/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameAccessibility.h"
AccessibilityInfo ownedFrameAccessibility(const IBattleInfo & state,
	const battle::BattleAccessibilityState & scene, battle::Units units)
{
	struct Context final : battle::BattleAccessibilityContext
	{
		const IBattleInfo & state;
		const battle::BattleAccessibilityState & scene;
		battle::Units units;
		Context(const IBattleInfo & state, const battle::BattleAccessibilityState & scene, battle::Units units)
			: state(state), scene(scene), units(std::move(units)) {}
		BattleHexArray impassableTiles() const override { return scene.impassable; }
		bool hasWalls() const override { return scene.walls; }
		EGateState gateState() const override { return state.getGateState(); }
		battle::Units aliveUnits() const override
		{
			battle::Units result;
			for(const auto * unit : units) if(unit->isValidTarget(false)) result.push_back(unit);
			return result;
		}
		std::vector<BattleHexArray> blockedObstacles() const override { return scene.obstacles; }
		EWallState wallState(EWallPart part) const override { return state.getWallState(part); }
		std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const override { return scene.reservations.at(side); }
	};
	return battle::buildBattleAccessibility(Context(state, scene, std::move(units)));
}
