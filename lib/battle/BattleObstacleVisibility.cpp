/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoEssentials.h"
#include "CObstacleInstance.h"

bool CBattleInfoEssentials::battleIsObstacleVisibleForSide(const CObstacleInstance & coi, BattleSide side) const
{
	RETURN_IF_NOT_BATTLE(false);
	return battleIsObstacleVisibleForSide(coi, side, side != BattleSide::ALL_KNOWING && battleHasNativeStack(side));
}

bool CBattleInfoEssentials::battleIsObstacleVisibleForSide(const CObstacleInstance & coi, BattleSide side, bool hasNativeStack)
{
	return side == BattleSide::ALL_KNOWING || coi.visibleForSide(side, hasNativeStack);
}
