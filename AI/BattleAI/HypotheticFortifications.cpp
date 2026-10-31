/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/SiegeInfo.h"

EWallState HypotheticBattle::getWallState(EWallPart part) const
{
	return projectedWalls.at(part);
}

int32_t HypotheticBattle::getWallStructuralHP(EWallPart part) const
{
	if(!canonicalStructuralHP)
		return 0;
	if(const auto it = projectedStructuralHP.find(part); it != projectedStructuralHP.end())
		return it->second;
	return 0;
}

EGateState HypotheticBattle::getGateState() const
{
	// The authoritative BattleProcessor derives this after catapult/earthquake
	// damage. A destroyed gate must not remain closed in the projected pathfinder.
	return getWallState(EWallPart::GATE) == EWallState::DESTROYED
		? EGateState::DESTROYED : initialGateState;
}

void HypotheticBattle::setWallState(EWallPart partOfWall, EWallState state)
{
	wallChanges |= projectedWalls[partOfWall] != state;
	projectedWalls[partOfWall] = state;
	if(canonicalStructuralHP && state == EWallState::DESTROYED)
		projectedStructuralHP[partOfWall] = 0;
}

void HypotheticBattle::setWallStructuralHP(EWallPart partOfWall, int32_t hp)
{
	if(!canonicalStructuralHP)
		return;

	const auto maximum = SiegeInfo::maximumStructuralHP(partOfWall);
	if(maximum <= 0)
		return;

	const auto bounded = std::clamp(hp, 0, maximum);
	wallChanges |= getWallStructuralHP(partOfWall) != bounded;
	projectedStructuralHP[partOfWall] = bounded;
	setWallState(partOfWall, SiegeInfo::stateFromStructuralHP(partOfWall, bounded));
}

void HypotheticBattle::openGateForReturn()
{
	wallChanges |= initialGateState != EGateState::OPENED;
	initialGateState = EGateState::OPENED;
}
