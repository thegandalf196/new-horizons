/*
 * MarchLeftTransfer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MarchLeftTransfer.h"

#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/pathfinder/NewHorizonsMarchLeft.h"

namespace NK2AI::marchLeftTransfer
{
bool strandsReceiver(const CGHeroInstance & receiver, const CArmedInstance & source, int day)
{
	std::vector<newHorizonsMarchLeft::StackTransfer> transfers;
	for(const auto & slot : source.Slots())
		transfers.push_back({&source, slot.first, &receiver, receiver.getSlotFor(slot.second->getCreature()), false});
	const auto effects = newHorizonsMarchLeft::projectTransfers(transfers, day);
	if(effects.empty())
		return false;
	const int stranded = newHorizonsMarchLeft::fullDailyMovement(receiver) * STRANDED_PERCENT / 100;
	return effects.front().movementBefore >= stranded && effects.front().movementAfter < stranded;
}
}
