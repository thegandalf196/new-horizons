/*
 * LiveBattleAccessibilityContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "LiveBattleAccessibilityContext.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "CObstacleInstance.h"
#include "../BattleFieldHandler.h"
#include "../entities/building/TownFortifications.h"

battle::LiveBattleAccessibilityContext::LiveBattleAccessibilityContext(const CBattleInfoCallback & callback)
	: callback(callback)
{
}

BattleHexArray battle::LiveBattleAccessibilityContext::impassableTiles() const
{
	const auto field = callback.battleGetBattlefieldType();
	return field == BattleField::NONE ? BattleHexArray{} : field.getInfo()->impassableHexes;
}

bool battle::LiveBattleAccessibilityContext::hasWalls() const
{
	return callback.battleGetFortifications().wallsHealth > 0;
}
EGateState battle::LiveBattleAccessibilityContext::gateState() const { return callback.battleGetGateState(); }
battle::Units battle::LiveBattleAccessibilityContext::aliveUnits() const { return callback.battleAliveUnits(); }
EWallState battle::LiveBattleAccessibilityContext::wallState(EWallPart part) const { return callback.battleGetWallState(part); }

std::vector<BattleHexArray> battle::LiveBattleAccessibilityContext::blockedObstacles() const
{
	std::vector<BattleHexArray> result;
	for(const auto & obstacle : callback.battleGetAllObstacles())
		result.push_back(obstacle->getBlockedTiles());
	return result;
}

std::vector<PendingDemonicGateFootprint> battle::LiveBattleAccessibilityContext::pendingGates(BattleSide side) const
{
	if(const auto * source = callback.getBattle())
		return source->getPendingDemonicGateFootprints(side);
	return {};
}

AccessibilityInfo CBattleInfoCallback::getAccessibility() const
{
	return battle::buildBattleAccessibility(battle::LiveBattleAccessibilityContext(*this));
}

AccessibilityInfo CBattleInfoCallback::getAccessibility(const BattleHexArray & accessibleHexes) const
{
	return battle::withAccessibleHexes(getAccessibility(), accessibleHexes);
}
