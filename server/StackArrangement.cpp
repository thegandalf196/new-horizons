/*
 * Part of VCMI engine
 * Authors: listed in AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/mapObjects/MiscObjects.h"
#include "../lib/mapObjects/CRewardableObject.h"
#include "../lib/networkPacks/StackLocation.h"

#include <limits>

void CGameHandler::notifyCannotMoveLastCreature(PlayerColor player)
{
	if(!player.isValidPlayer())
		return;
	const auto * playerState = gameInfo().getPlayerState(player, false);
	if(!playerState || playerState->status != EPlayerStatus::INGAME)
		return;

	InfoWindow notification;
	notification.player = player;
	notification.text = MetaString::createFromTextID("core.tcommand.5");
	sendAndApply(notification);
}

class CGameHandler::StackArrangement
{
	CGameHandler & handler;
	const CArmedInstance * const s1;
	const CArmedInstance * const s2;
	const StackLocation sl1;
	const StackLocation sl2;
	const PlayerColor player;
	const bool crossArmy;

	bool notRemovable(const CArmedInstance * army);
public:
	StackArrangement(CGameHandler & handler, const CArmedInstance * source, const CArmedInstance * destination,
		SlotID from, SlotID to, PlayerColor player, bool crossArmy)
		: handler(handler), s1(source), s2(destination), sl1(source->id, from), sl2(destination->id, to),
		  player(player), crossArmy(crossArmy)
	{
	}
	bool swap();
	bool merge();
	bool split(si32 val);
};

bool CGameHandler::StackArrangement::notRemovable(const CArmedInstance * army)
{
	// We can put stacks into locked garrisons and rearrange within them,
	// but cannot take their troops into another army.
	if(crossArmy)
	{
		const auto * g = dynamic_cast<const CGGarrison *>(army);
		if(g && !g->removableUnits)
		{
			handler.complain("Stacks in this garrison are not removable!\n");
			return true;
		}
	}
	return false;
}

bool CGameHandler::StackArrangement::swap()
{
	const auto p1 = sl1.slot;
	const auto p2 = sl2.slot;
	if (((s1->tempOwner != player && s1->tempOwner != PlayerColor::UNFLAGGABLE) && s1->getStackCount(p1))
	  || ((s2->tempOwner != player && s2->tempOwner != PlayerColor::UNFLAGGABLE) && s2->getStackCount(p2)))
	{
		handler.complain("Can't take troops from another player!");
		return false;
	}

	if (sl1.army == sl2.army && sl1.slot == sl2.slot)
	{
		handler.complain("Cannot swap stacks - slots are the same!");
		return false;
	}

	if (!s1->slotEmpty(p1) && !s2->slotEmpty(p2))
	{
		if (notRemovable(s1) || notRemovable(s2))
			return false;
	}
	if (s1->slotEmpty(p1) && notRemovable(s2))
		return false;
	else if (s2->slotEmpty(p2) && notRemovable(s1))
		return false;

	return handler.swapStacks(sl1, sl2);

}

bool CGameHandler::StackArrangement::merge()
{
	const auto p1 = sl1.slot;
	const auto p2 = sl2.slot;
	if (sl1.army == sl2.army && sl1.slot == sl2.slot)
	{
		handler.complain("Cannot merge a stack with itself!");
		return false;
	}

	if ((s1->getCreature(p1) != s2->getCreature(p2) && handler.complain("Cannot merge different creatures stacks!"))
	|| (((s1->tempOwner != player && s1->tempOwner != PlayerColor::UNFLAGGABLE) && s2->getStackCount(p2)) && handler.complain("Can't take troops from another player!")))
		return false;

	if (s1->slotEmpty(p1) || s2->slotEmpty(p2))
	{
		handler.complain("Cannot merge empty stack!");
		return false;
	}
	else if (notRemovable(s1))
		return false;

	const TQuantity sourceCount = s1->getStackCount(p1);
	const TQuantity destinationCount = s2->getStackCount(p2);
	int64_t maximumCount = std::numeric_limits<TQuantity>::max();
	const int64_t availableCapacity = std::max<int64_t>(0, maximumCount - destinationCount);
	int64_t transferCount = std::min<int64_t>(sourceCount, availableCapacity);
	const bool mustKeepLastSourceCreature = crossArmy && s1->needsLastStack() && s1->stacksCount() == 1;
	if(mustKeepLastSourceCreature)
		transferCount = std::min<int64_t>(transferCount, sourceCount - 1);

	if(transferCount == 0)
	{
		// Keeping the last creature in a required source army takes precedence
		// over a simultaneous zero-capacity destination.
		if(mustKeepLastSourceCreature && sourceCount == 1)
		{
			if(player == s1->tempOwner)
				handler.notifyCannotMoveLastCreature(player);
			return false;
		}
		if(destinationCount == std::numeric_limits<TQuantity>::max())
			handler.complain("Cannot exceed the maximum stack size!");
		else if(mustKeepLastSourceCreature)
			handler.complain("Cannot move away the last creature!");
		else
			handler.complain("Cannot merge these stacks!");
		return false;
	}

	return handler.moveStack(sl1, sl2, static_cast<TQuantity>(transferCount));

}

bool CGameHandler::StackArrangement::split(si32 val)
{
	const auto p1 = sl1.slot;
	const auto p2 = sl2.slot;
	const CCreatureSet & S1 = *s1;
	const CCreatureSet & S2 = *s2;
	if(!vstd::contains(S1.stacks, p1) && handler.complain(handler.complainNoCreatures))
		return false;
	if(val < 1 && handler.complain(handler.complainNoCreatures))
		return false;

	const int64_t sourceCount = s1->getStackCount(p1);
	const int64_t destinationCount = s2->getStackCount(p2);
	const int64_t countToMove = static_cast<int64_t>(val) - destinationCount;
	if(vstd::contains(S2.stacks, p2) && countToMove < 0)
	{
		handler.complain("Cannot reduce the destination stack with a split request!");
		return false;
	}
	if(vstd::contains(S2.stacks, p2) && countToMove == 0)
		return true;
	if(countToMove < 0 || countToMove > sourceCount)
	{
		handler.complain("Cannot split that stack, not enough creatures!");
		return false;
	}
	const int64_t countLeftOnSrc = sourceCount - countToMove;

	const auto * bank = dynamic_cast<const CRewardableObject *>(s1);
	const bool takingBankLoot = bank && bank->hasPendingBankCreatureReward()
		&& crossArmy && s2->tempOwner == player;
	if (  (s1->tempOwner != player && !takingBankLoot && countLeftOnSrc < s1->getStackCount(p1))
		|| (s2->tempOwner != player && val < s2->getStackCount(p2)))
	{
		handler.complain("Can't move troops of another player!");
		return false;
	}

	if (vstd::contains(S2.stacks,p2))	 //dest. slot not free - it must be "rebalancing"...
	{
		const int64_t total = sourceCount + destinationCount;
		if ((total < val   &&   handler.complain("Cannot split that stack, not enough creatures!"))
			|| (s1->getCreature(p1) != s2->getCreature(p2) && handler.complain("Cannot rebalance different creatures stacks!"))
		)
		{
			return false;
		}

		if (notRemovable(s1))
		{
			if (s1->getStackCount(p1) > countLeftOnSrc)
				return false;
		}
		else if (notRemovable(s2))
		{
			if (s2->getStackCount(p1) < countLeftOnSrc)
				return false;
		}

		return handler.moveStack(sl1, sl2, static_cast<TQuantity>(countToMove));
		//S2.slots[p2]->count = val;
		//S1.slots[p1]->count = total - val;
	}
	else //split one stack to the two
	{
		if (s1->getStackCount(p1) < val)//not enough creatures
		{
			handler.complain(handler.complainNotEnoughCreatures);
			return false;
		}

		if (notRemovable(s1))
			return false;

		return handler.moveStack(sl1, sl2, val);
	}


}

bool CGameHandler::arrangeStacks(ObjectInstanceID id1, ObjectInstanceID id2, ui8 what, SlotID p1, SlotID p2, si32 val, PlayerColor player)
{
	const auto * s1 = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(id1));
	const auto * s2 = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(id2));

	if (s1 == nullptr || s2 == nullptr)
	{
		complain("Cannot exchange stacks between non-existing objects!!\n");
		return false;
	}

	const auto protectedSlot = [](const CArmedInstance * army, SlotID slot)
	{
		const auto * mine = dynamic_cast<const CGMine *>(army);
		return mine && mine->isCompanySlot(slot);
	};
	if(protectedSlot(s1, p1) || protectedSlot(s2, p2))
	{
		complain("Mercenary Company defenders cannot be rearranged or transferred");
		return false;
	}
	StackLocation sl1(s1->id, p1);
	StackLocation sl2(s2->id, p2);

	if (!sl1.slot.validSlot()  ||  !sl2.slot.validSlot())
	{
		complain(complainInvalidSlot);
		return false;
	}

	if (!isAllowedExchange(id1,id2))
	{
		complain("Cannot exchange stacks between these two objects!\n");
		return false;
	}

	StackArrangement arrangement(*this, s1, s2, p1, p2, player, id1 != id2);
	switch(what)
	{
	case 1: return arrangement.swap();
	case 2: return arrangement.merge();
	case 3: return arrangement.split(val);
	default: return true;
	}
}
