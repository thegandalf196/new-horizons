/*
 * CreatureRewardTransfer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGameHandler.h"

#include "../lib/CStack.h"
#include "../lib/mapObjects/CRewardableObject.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/networkPacks/StackLocation.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
// Fill matching slots before opening new ones, respecting independent slot limits.
// Snapshot source slots: a complete move erases its stack from the source map.
void collectBankCreatures(CGameHandler & handler, const CArmedInstance & source, const CGHeroInstance & hero)
{
	std::vector<SlotID> sourceSlots;
	for(const auto & [slot, stack] : source.Slots())
		sourceSlots.push_back(slot);

	for(const auto sourceSlot : sourceSlots)
	{
		const auto creature = source.getStackPtr(sourceSlot)->getCreatureID();
		for(int pass = 0; pass < 2 && source.hasStackAtSlot(sourceSlot); ++pass)
		{
			for(int i = 0; i < GameConstants::ARMY_SIZE && source.hasStackAtSlot(sourceSlot); ++i)
			{
				const SlotID destination(i);
				const bool occupied = hero.hasStackAtSlot(destination);
				if(pass == 0 ? !occupied || hero.getStackPtr(destination)->getCreatureID() != creature : occupied)
					continue;
				const auto currentCount = occupied ? hero.getStackCount(destination) : 0;
				auto count = std::min(source.getStackCount(sourceSlot), std::numeric_limits<TQuantity>::max() - currentCount);
				if(const auto capacity = hero.getLeadershipSlotCapacity(creature))
					count = std::max<TQuantity>(0, std::min<TQuantity>(count,
						capacity->maximum - currentCount));
				if(count > 0 && !handler.moveStack(StackLocation(source.id, sourceSlot),
					StackLocation(hero.id, destination), count))
					return;
			}
		}
	}
}
}

void CGameHandler::giveCreatures(const CArmedInstance * obj, const CGHeroInstance * hero, const CCreatureSet & creatures, bool remove)
{
	if(!creatures.stacksCount() || obj->stacksCount() || creatures.stacksCount() > GameConstants::ARMY_SIZE)
	{
		complain("Cannot give creatures: empty reward, uncleared source, or too many reward stacks.");
		return;
	}
	const auto * bank = dynamic_cast<const CRewardableObject *>(obj);
	const bool bankReward = bank && bank->ID == Obj::CREATURE_BANK
		&& std::ranges::any_of(creatures.Slots(), [hero](const auto & entry)
		{
			return hero->getLeadershipSlotCapacity(entry.second->getCreatureID()).has_value();
		});
	// Keep the existing nonbank and direct-reward admission contracts. Banks can
	// retain a partial reward and therefore must reach the validated exchange.
	if(!bankReward && hero->canBeMergedWith(creatures, true) && !validateLeadershipArmyAddition(hero, creatures))
		return;

	if(bankReward)
		setObjPropertyValue(obj->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, true);
	for(const auto & [slot, stack] : creatures.Slots())
	{
		// Keep bank reward slots distinct, including duplicate creature entries.
		// Coalescing them here could overflow before validated intake begins.
		const auto sourceSlot = bankReward ? obj->getFreeSlot() : obj->getSlotFor(stack->getCreature());
		addToSlot(StackLocation(obj->id, sourceSlot), stack->getCreature(), stack->getCount());
	}

	if(bankReward)
	{
		collectBankCreatures(*this, *obj, *hero);
		if(obj->stacksCount() == 0)
		{
			setObjPropertyValue(obj->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, false);
			return;
		}
	}
	tryJoiningArmy(obj, hero, remove, true);
	if(bankReward && obj->stacksCount() == 0)
		setObjPropertyValue(obj->id, ObjProperty::BANK_PENDING_CREATURE_REWARD, false);
}
