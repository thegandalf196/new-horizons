/*
 * Part of VCMI engine
 * Authors: listed in AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/pathfinder/NewHorizonsMarchLeft.h"

#include <map>

// A hero who receives troops from another army keeps at most the share of his own
// daily movement that the received stack with the least march left still has. The
// giver and moves inside one army are unaffected. Runs after the operation has been
// applied, so each destination slot already holds the moved (and possibly merged) stack.
void CGameHandler::applyMarchLeftLimits(const CGarrisonOperationPack & pack)
{
	const int day = gameInfo().getCalendar().getCurrentDay();
	std::map<ObjectInstanceID, int> received;
	const auto receive = [&](ObjectInstanceID from, ObjectInstanceID to, SlotID slot)
	{
		if(from == to)
			return;
		const auto * hero = gameInfo().getHero(to);
		if(!hero || !hero->hasStackAtSlot(slot))
			return;
		const int left = hero->getStack(slot).getMarchLeft(day);
		if(const auto * giver = gameInfo().getHero(from))
			logGlobal->info("Day's March: %s took a stack from hero %s (march left %d) with %d movement",
				hero->getNameTextID(), giver->getNameTextID(), left, hero->movementPointsRemaining());
		const auto [entry, added] = received.try_emplace(to, left);
		if(!added)
			entry->second = std::min(entry->second, left);
	};
	if(const auto * move = dynamic_cast<const RebalanceStacks *>(&pack))
		receive(move->srcArmy, move->dstArmy, move->dstSlot);
	else if(const auto * moves = dynamic_cast<const BulkRebalanceStacks *>(&pack))
	{
		for(const auto & entry : moves->moves)
			receive(entry.srcArmy, entry.dstArmy, entry.dstSlot);
	}
	else if(const auto * swap = dynamic_cast<const SwapStacks *>(&pack))
	{
		receive(swap->srcArmy, swap->dstArmy, swap->dstSlot);
		receive(swap->dstArmy, swap->srcArmy, swap->srcSlot);
	}

	for(const auto & [heroID, left] : received)
	{
		const auto * hero = gameInfo().getHero(heroID);
		const auto limited = hero ? newHorizonsMarchLeft::limitedMovement(*hero, left) : std::nullopt;
		if(!limited)
			continue;
		logGlobal->info("Day's March: %s limited from %d to %d movement", hero->getNameTextID(),
			hero->movementPointsRemaining(), *limited);
		SetMovePoints limit(heroID, *limited);
		limit.marchLeftLimit = true;
		sendAndApply(limit);
	}
}

int CGameHandler::lowestMarchLeft(const CArmedInstance & army, const std::vector<SlotID> & slots)
{
	const int day = gameInfo().getCalendar().getCurrentDay();
	int marchLeft = newHorizonsMarchLeft::FULL;
	for(const auto slot : slots)
		if(army.hasStackAtSlot(slot))
			marchLeft = std::min(marchLeft, army.getStack(slot).getMarchLeft(day));
	return marchLeft;
}

void CGameHandler::carryMarchLeft(const CArmedInstance & army, SlotID slot, int marchLeft)
{
	const int day = gameInfo().getCalendar().getCurrentDay();
	if(marchLeft >= newHorizonsMarchLeft::FULL || !army.hasStackAtSlot(slot) || army.getStack(slot).getMarchLeft(day) <= marchLeft)
		return;
	LowerStackMarchLeft carried;
	carried.army = army.id;
	carried.slot = slot;
	carried.day = day;
	carried.marchLeft = marchLeft;
	sendAndApply(carried);
}
