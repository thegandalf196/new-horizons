/*
 * LiveBattleAccessibilityContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "BattleAccessibilityQuery.h"

class CBattleInfoCallback;
namespace battle
{
/// Lazy original callback adapter; keeps player obstacle visibility unchanged.
class DLL_LINKAGE LiveBattleAccessibilityContext final : public BattleAccessibilityContext
{
	const CBattleInfoCallback & callback;
public:
	explicit LiveBattleAccessibilityContext(const CBattleInfoCallback & callback);
	BattleHexArray impassableTiles() const override;
	bool hasWalls() const override;
	EGateState gateState() const override;
	Units aliveUnits() const override;
	std::vector<BattleHexArray> blockedObstacles() const override;
	EWallState wallState(EWallPart part) const override;
	std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const override;
};
}
