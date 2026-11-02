/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/SiegeInfo.h"
void HypotheticBattle::captureFortificationState(const CBattleInfoCallback & actualSource)
{
	// Root reads use the actual source while its preparation lease is held.
	// An owned child uses its parent's already captured projection directly:
	// callback wall/gate wrappers would request an uncaptured defended town.
	const auto & source = ownedFrameBatch ? actualSource : *subject; // Preserve the legacy forked-source route.
	const auto * parent = dynamic_cast<const HypotheticBattle *>(source.getBattle());
	const bool ownedParent = parent && parent->usesOwnedFrameQueries();
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		projectedWalls[part] = ownedParent ? parent->getWallState(part) : source.battleGetWallState(part);
		projectedStructuralHP[part] = ownedParent ? parent->getWallStructuralHP(part) : source.getWallStructuralHP(part);
		canonicalStructuralHP |= projectedStructuralHP[part] > 0;
	}
	initialGateState = ownedParent ? parent->getGateState() : source.battleGetGateState();
	// Use the subject's visible view, not its unfiltered authoritative obstacle list.
	if(!ownedFrameBatch) for(const auto & obstacle : BattleProxy::getAllObstacles())
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
			projectedObstacles.push_back(std::make_shared<SpellCreatedObstacle>(*spell));
		else
			projectedObstacles.push_back(obstacle);
	}

}
