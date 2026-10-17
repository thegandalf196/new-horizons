/*
 * BattleAccessibilityQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "AccessibilityInfo.h"
#include "IBattleInfoCallback.h"
#include <span>
#include <utility>

struct PendingDemonicGateFootprint;

namespace battle
{
/// Complete accessibility inputs only. Unit/obstacle order and source visibility
/// belong to the adapter; shared tile precedence belongs to the query owner.
class DLL_LINKAGE BattleAccessibilityContext
{
public:
	virtual ~BattleAccessibilityContext() = default;
	virtual BattleHexArray impassableTiles() const = 0;
	virtual bool hasWalls() const = 0;
	virtual EGateState gateState() const = 0;
	virtual Units aliveUnits() const = 0;
	virtual std::vector<BattleHexArray> blockedObstacles() const = 0;
	virtual EWallState wallState(EWallPart part) const = 0;
	virtual std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const = 0;
};

DLL_LINKAGE AccessibilityInfo buildBattleAccessibility(const BattleAccessibilityContext & context);
/// Existing explicit known-accessible override; pending reservations still win.
DLL_LINKAGE AccessibilityInfo withAccessibleHexes(AccessibilityInfo accessibility, const BattleHexArray & hexes);
/// The same four destructible wall parts used by both capture and construction.
DLL_LINKAGE std::span<const std::pair<EWallPart, BattleHex>> accessibilityWallParts();
}
