/*
 * SiegeInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SiegeInfo.h"


SiegeInfo::SiegeInfo()
{
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
	{
		wallState[static_cast<EWallPart>(i)] = EWallState::NONE;
	}
	gateState = EGateState::NONE;
}

int32_t SiegeInfo::maximumStructuralHP(EWallPart part)
{
	switch(part)
	{
		case EWallPart::BOTTOM_WALL:
		case EWallPart::BELOW_GATE:
		case EWallPart::OVER_GATE:
		case EWallPart::UPPER_WALL:
			return 300;
		case EWallPart::GATE:
			return 450;
		case EWallPart::KEEP:
		case EWallPart::BOTTOM_TOWER:
		case EWallPart::UPPER_TOWER:
			return 350;
		default:
			return 0;
	}
}

EWallState SiegeInfo::stateFromStructuralHP(EWallPart part, int32_t hp)
{
	const int32_t maximum = maximumStructuralHP(part);
	if(maximum <= 0 || hp <= 0)
		return EWallState::DESTROYED;
	return hp * 2 <= maximum ? EWallState::DAMAGED : EWallState::INTACT;
}

EWallState SiegeInfo::applyDamage(EWallState state, unsigned int value)
{
	if(state == EWallState::NONE)
		return EWallState::NONE;

	// wall health is stored as EWallState value and may exceed REINFORCED for extra-fortified walls,
	// so decrement numerically instead of stepping through named states
	int reduced = static_cast<int>(state) - static_cast<int>(value);
	return static_cast<EWallState>(std::max(reduced, static_cast<int>(EWallState::DESTROYED)));
}
