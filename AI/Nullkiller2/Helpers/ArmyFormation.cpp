/*
* ArmyFormation.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "ArmyFormation.h"
#include "../../../lib/mapObjects/CGTownInstance.h"

namespace NK2AI
{

namespace armyFormation
{

bool canReceiveStack(const CArmedInstance * destination, CreatureID creature, int resultingCount)
{
	if(!destination || !creature.hasValue() || resultingCount < 0)
		return false;

	const auto * hero = dynamic_cast<const CGHeroInstance *>(destination);
	if(!hero)
		return true;

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	return !capacity || capacity->accepts(resultingCount);
}

namespace
{

bool sourceWouldLoseLastStack(const CArmedInstance * source, const CArmedInstance * destination,
	int transferCount, int sourceCount)
{
	return source != destination && source->needsLastStack() && source->stacksCount() == 1
		&& transferCount >= sourceCount;
}

int maximumDestinationAddition(const CArmedInstance * destination, CreatureID creature, int existingCount,
	int sourceCount)
{
	if(!destination || !creature.hasValue() || existingCount < 0 || sourceCount < 0)
		return 0;

	const auto * hero = dynamic_cast<const CGHeroInstance *>(destination);
	if(!hero)
		return sourceCount;

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	if(!capacity)
		return sourceCount;

	return std::min(sourceCount, std::max(0, capacity->maximum - existingCount));
}

}

bool canSwapStacks(const CArmedInstance * first, const CArmedInstance * second,
	SlotID firstSlot, SlotID secondSlot)
{
	if(!first || !second || !firstSlot.validSlot() || !secondSlot.validSlot())
		return false;

	const auto * firstStack = first->getStackPtr(firstSlot);
	const auto * secondStack = second->getStackPtr(secondSlot);
	if(!firstStack && !secondStack)
		return false;

	if(firstStack && secondStack)
	{
		return canReceiveStack(second, firstStack->getCreatureID(), firstStack->getCount())
			&& canReceiveStack(first, secondStack->getCreatureID(), secondStack->getCount());
	}

	if(firstStack)
	{
		if(sourceWouldLoseLastStack(first, second, firstStack->getCount(), firstStack->getCount()))
			return false;
		return canReceiveStack(second, firstStack->getCreatureID(), firstStack->getCount());
	}

	if(sourceWouldLoseLastStack(second, first, secondStack->getCount(), secondStack->getCount()))
		return false;
	return canReceiveStack(first, secondStack->getCreatureID(), secondStack->getCount());
}

bool canMergeOrSwapStacks(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot)
{
	if(!source || !destination || !sourceSlot.validSlot() || !destinationSlot.validSlot())
		return false;

	const auto * incoming = source->getStackPtr(sourceSlot);
	const auto * existing = destination->getStackPtr(destinationSlot);
	if(!incoming || !existing)
		return canSwapStacks(source, destination, sourceSlot, destinationSlot);

	if(existing->getCreatureID() == incoming->getCreatureID())
	{
		if(sourceWouldLoseLastStack(source, destination, incoming->getCount(), incoming->getCount()))
			return false;
		return canReceiveStack(destination, incoming->getCreatureID(),
			incoming->getCount() + existing->getCount());
	}

	return canSwapStacks(source, destination, sourceSlot, destinationSlot);
}

bool canSplitStack(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot, int resultingDestinationCount)
{
	if(!source || !destination || !sourceSlot.validSlot() || !destinationSlot.validSlot())
		return false;

	const auto * incoming = source->getStackPtr(sourceSlot);
	if(!incoming || resultingDestinationCount < 1)
		return false;

	const auto * existing = destination->getStackPtr(destinationSlot);
	if(existing && existing->getCreatureID() != incoming->getCreatureID())
		return false;
	const int existingCount = existing ? existing->getCount() : 0;
	const int transferCount = resultingDestinationCount - existingCount;
	if(transferCount < 1 || transferCount > incoming->getCount()
		|| sourceWouldLoseLastStack(source, destination, transferCount, incoming->getCount()))
		return false;

	return canReceiveStack(destination, incoming->getCreatureID(), resultingDestinationCount);
}

int maxLegalTransferCount(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot)
{
	if(!source || !destination || !sourceSlot.validSlot() || !destinationSlot.validSlot())
		return 0;

	const auto * incoming = source->getStackPtr(sourceSlot);
	if(!incoming)
		return 0;

	const auto * existing = destination->getStackPtr(destinationSlot);
	if(existing && existing->getCreatureID() != incoming->getCreatureID())
		return canSwapStacks(source, destination, sourceSlot, destinationSlot)
			? incoming->getCount() : 0;

	const int existingCount = existing ? existing->getCount() : 0;
	int result = maximumDestinationAddition(destination, incoming->getCreatureID(), existingCount,
		incoming->getCount());
	if(sourceWouldLoseLastStack(source, destination, result, incoming->getCount()))
		result = std::min(result, std::max(0, incoming->getCount() - 1));
	return result;
}

}

void ArmyFormation::rearrangeArmyForWhirlpool(const CGHeroInstance * hero)
{
	addSingleCreatureStacks(hero);
}

void ArmyFormation::addSingleCreatureStacks(const CGHeroInstance * hero)
{
	auto freeSlots = hero->getFreeSlots();

	while(!freeSlots.empty())
	{
		TSlots::const_iterator weakestCreature = vstd::minElementByFun(hero->Slots(), [](const auto & slot) -> int
			{
				return slot.second->getCount() == 1
					? std::numeric_limits<int>::max()
					: slot.second->getCreatureID().toCreature()->getAIValue();
			});

		if(weakestCreature == hero->Slots().end() || weakestCreature->second->getCount() == 1)
		{
			break;
		}

		if(!armyFormation::canSplitStack(hero, hero, weakestCreature->first, freeSlots.back(), 1))
			break;

		cb->splitStack(hero, hero, weakestCreature->first, freeSlots.back(), 1);
		freeSlots.pop_back();
	}
}

void ArmyFormation::rearrangeArmyForSiege(const CGTownInstance * town, const CGHeroInstance * attacker)
{
	addSingleCreatureStacks(attacker);

	if(town->fortLevel() > CGTownInstance::FORT)
	{
		std::vector<const CStackInstance *> stacks;

		for(const auto & slot : attacker->Slots())
			stacks.push_back(slot.second.get());

		std::ranges::sort(
			stacks,
			[](const CStackInstance * slot1, const CStackInstance * slot2) -> bool
			{
				auto cre1 = slot1->getCreatureID().toCreature();
				auto cre2 = slot2->getCreatureID().toCreature();
				auto flying = cre1->hasBonusOfType(BonusType::FLYING) - cre2->hasBonusOfType(BonusType::FLYING);
			
				if(flying != 0) return flying < 0;
				else return cre1->getAIValue() < cre2->getAIValue();
			});

		for(int i = 0; i < stacks.size(); i++)
		{
			auto pos = stacks[i]->getArmy()->findStack(stacks[i]);

			if(pos.getNum() != i
				&& armyFormation::canSwapStacks(attacker, attacker, static_cast<SlotID>(i), pos))
				cb->swapCreatures(attacker, attacker, static_cast<SlotID>(i), pos);
		}
	}
}

}
