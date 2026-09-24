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

namespace
{

struct ProjectedSlotState
{
	CreatureID creature = CreatureID::NONE;
	int count = 0;
	bool occupied = false;
};

using ProjectedArmyState = std::array<ProjectedSlotState, GameConstants::ARMY_SIZE>;

ProjectedArmyState makeProjectedArmy(const CCreatureSet * army)
{
	ProjectedArmyState result{};
	if(!army)
		return result;

	for(const auto & [slot, stack] : army->Slots())
		if(slot.validSlot() && static_cast<size_t>(slot.getNum()) < GameConstants::ARMY_SIZE)
			result[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};

	return result;
}

int projectedStackCount(const ProjectedArmyState & army)
{
	return std::ranges::count_if(army, [](const ProjectedSlotState & slot) { return slot.occupied; });
}

bool acceptsStack(const CGHeroInstance * hero, CreatureID creature, int count)
{
	if(!hero || !creature.hasValue() || count < 0)
		return count >= 0;

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	return !capacity || capacity->accepts(count);
}

int maximumAddition(const CGHeroInstance * hero, CreatureID creature, int currentCount, int availableCount)
{
	if(currentCount < 0 || availableCount < 0)
		return 0;

	if(!hero)
		return std::min(availableCount, std::numeric_limits<int>::max() - currentCount);

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	if(!capacity)
		return std::min(availableCount, std::numeric_limits<int>::max() - currentCount);

	return std::min(availableCount, std::max(0, capacity->maximum - currentCount));
}

bool canApplyProjectedTransfer(
	const ProjectedArmyState & source,
	const ProjectedArmyState & destination,
	SlotID sourceSlot,
	SlotID destinationSlot,
	const CGHeroInstance * sourceHero,
	const CGHeroInstance * destinationHero,
	bool sourceNeedsLastStack,
	int & maximumTransfer,
	bool & swapsStacks)
{
	maximumTransfer = 0;
	swapsStacks = false;
	if(!sourceSlot.validSlot() || !destinationSlot.validSlot())
		return false;

	const auto & incoming = source[sourceSlot.getNum()];
	const auto & existing = destination[destinationSlot.getNum()];
	if(!incoming.occupied)
		return false;

	if(existing.occupied && existing.creature != incoming.creature)
	{
		if(!acceptsStack(destinationHero, incoming.creature, incoming.count)
			|| !acceptsStack(sourceHero, existing.creature, existing.count))
			return false;

		maximumTransfer = incoming.count;
		swapsStacks = true;
		return true;
	}

	const int existingCount = existing.occupied ? existing.count : 0;
	maximumTransfer = maximumAddition(destinationHero, incoming.creature, existingCount, incoming.count);
	if(sourceNeedsLastStack && projectedStackCount(source) == 1)
		maximumTransfer = std::min(maximumTransfer, std::max(0, incoming.count - 1));

	return maximumTransfer > 0;
}

void applyProjectedTransfer(
	ArmyExchangeProjection & projection,
	ProjectedArmyState & receiver,
	ProjectedArmyState & source,
	ArmyExchangeSide sourceSide,
	SlotID sourceSlot,
	SlotID destinationSlot,
	int requestedCount,
	const CGHeroInstance * receiverHero,
	const CGHeroInstance * sourceHero,
	bool receiverNeedsLastStack,
	bool sourceNeedsLastStack)
{
	const bool fromReceiver = sourceSide == ArmyExchangeSide::RECEIVER;
	auto & fromArmy = fromReceiver ? receiver : source;
	auto & toArmy = fromReceiver ? source : receiver;
	const auto * fromHero = fromReceiver ? receiverHero : sourceHero;
	const auto * toHero = fromReceiver ? sourceHero : receiverHero;
	const bool fromNeedsLastStack = fromReceiver ? receiverNeedsLastStack : sourceNeedsLastStack;
	int maximumTransfer = 0;
	bool swapsStacks = false;
	if(!canApplyProjectedTransfer(fromArmy, toArmy, sourceSlot, destinationSlot,
		fromHero, toHero, fromNeedsLastStack, maximumTransfer, swapsStacks))
		return;

	auto & from = fromArmy[sourceSlot.getNum()];
	auto & to = toArmy[destinationSlot.getNum()];
	const int transferCount = swapsStacks ? from.count : std::min(maximumTransfer, requestedCount);
	if(transferCount <= 0)
		return;

	ArmyExchangeTransfer transfer;
	transfer.sourceSide = sourceSide;
	transfer.sourceSlot = sourceSlot;
	transfer.destinationSlot = destinationSlot;
	transfer.expectedSourceCreature = from.creature;
	transfer.expectedSourceCount = from.count;
	transfer.expectedDestinationCreature = to.occupied ? to.creature : CreatureID::NONE;
	transfer.expectedDestinationCount = to.occupied ? to.count : 0;
	transfer.transferCount = transferCount;
	transfer.swapsStacks = swapsStacks;

	if(swapsStacks)
	{
		transfer.resultingDestinationCount = from.count;
		std::swap(fromArmy[sourceSlot.getNum()], toArmy[destinationSlot.getNum()]);
	}
	else
	{
		transfer.resultingDestinationCount = (to.occupied ? to.count : 0) + transferCount;
		if(!to.occupied)
			to = {from.creature, transferCount, true};
		else
			to.count += transferCount;
		from.count -= transferCount;
		if(from.count == 0)
			from = {};
	}

	projection.transfers.push_back(transfer);
}

void appendProjectedSlots(const ProjectedArmyState & army, std::vector<ProjectedArmyStack> & result)
{
	for(size_t i = 0; i < army.size(); ++i)
		if(army[i].occupied)
			result.push_back({static_cast<SlotID>(i), army[i].creature, army[i].count});
}

}

bool hasLeadershipCapacityRules(const CGHeroInstance * hero, const CCreatureSet * army)
{
	if(!hero || !army)
		return false;

	for(const auto & slot : army->Slots())
		if(hero->getLeadershipSlotCapacity(slot.second->getCreatureID()))
			return true;

	return false;
}

ArmyExchangeProjection projectArmyExchange(
	const CCreatureSet * receiverArmy,
	const CCreatureSet * sourceArmy,
	const CGHeroInstance * receiverHero,
	const CGHeroInstance * sourceHero,
	const std::vector<DesiredArmyStack> & desiredReceiver)
{
	ArmyExchangeProjection result;
	auto receiver = makeProjectedArmy(receiverArmy);
	auto source = makeProjectedArmy(sourceArmy);
	if(!receiverArmy || !sourceArmy || receiverArmy == sourceArmy)
	{
		appendProjectedSlots(receiver, result.receiverSlots);
		appendProjectedSlots(source, result.sourceSlots);
		return result;
	}

	struct DesiredTarget
	{
		DesiredArmyStack stack;
		SlotID slot;
	};
	std::vector<DesiredTarget> targets;
	std::set<CreatureID> desiredTypes;
	std::array<bool, GameConstants::ARMY_SIZE> reservedReceiverSlots{};

	for(const auto & desired : desiredReceiver)
	{
		if(!desired.creature.hasValue() || desired.count <= 0)
			continue;

		desiredTypes.insert(desired.creature);
		SlotID matching;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(!reservedReceiverSlots[i] && receiver[i].occupied && receiver[i].creature == desired.creature)
			{
				matching = static_cast<SlotID>(i);
				break;
			}

		if(matching.validSlot())
		{
			reservedReceiverSlots[matching.getNum()] = true;
			targets.push_back({desired, matching});
		}
		else
			targets.push_back({desired, SlotID()});
	}

	// Assign new desired stacks to empty receiver slots first. Duplicate creature
	// entries remain separate slots and therefore get separate capacity checks.
	for(auto & target : targets)
	{
		if(target.slot.validSlot())
			continue;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(!reservedReceiverSlots[i] && !receiver[i].occupied)
			{
				target.slot = static_cast<SlotID>(i);
				reservedReceiverSlots[i] = true;
				break;
			}
	}

	// If no empty slot remains, a desired source stack may replace an undesired
	// receiver stack only when both heroes can legally receive the exchanged stacks.
	for(auto & target : targets)
	{
		if(target.slot.validSlot())
			continue;

		bool swapped = false;
		for(int destinationIndex = 0; destinationIndex < GameConstants::ARMY_SIZE && !swapped; ++destinationIndex)
		{
			if(reservedReceiverSlots[destinationIndex] || !receiver[destinationIndex].occupied
				|| desiredTypes.contains(receiver[destinationIndex].creature))
				continue;

			for(int sourceIndex = 0; sourceIndex < GameConstants::ARMY_SIZE; ++sourceIndex)
			{
				if(!source[sourceIndex].occupied || source[sourceIndex].creature != target.stack.creature)
					continue;

				const SlotID sourceSlot = static_cast<SlotID>(sourceIndex);
				const SlotID destinationSlot = static_cast<SlotID>(destinationIndex);
				const size_t transferCountBefore = result.transfers.size();
				applyProjectedTransfer(result, receiver, source, ArmyExchangeSide::SOURCE,
					sourceSlot, destinationSlot, target.stack.count, receiverHero, sourceHero,
					receiverArmy->needsLastStack(), sourceArmy->needsLastStack());
				if(result.transfers.size() != transferCountBefore)
				{
					target.slot = destinationSlot;
					reservedReceiverSlots[destinationIndex] = true;
					swapped = true;
					break;
				}
			}
		}
	}

	// Return receiver stacks that the preferred army no longer wants. A failed
	// return leaves the stack in place; troops are never dismissed by projection.
	for(int receiverIndex = 0; receiverIndex < GameConstants::ARMY_SIZE; ++receiverIndex)
	{
		if(reservedReceiverSlots[receiverIndex] || !receiver[receiverIndex].occupied
			|| desiredTypes.contains(receiver[receiverIndex].creature))
			continue;

		for(int sourceIndex = 0; sourceIndex < GameConstants::ARMY_SIZE && receiver[receiverIndex].occupied; ++sourceIndex)
		{
			if(source[sourceIndex].occupied && source[sourceIndex].creature != receiver[receiverIndex].creature)
				continue;

			const SlotID receiverSlot = static_cast<SlotID>(receiverIndex);
			const SlotID sourceSlot = static_cast<SlotID>(sourceIndex);
			const int requestedCount = receiver[receiverIndex].count;
			const size_t transferCountBefore = result.transfers.size();
			applyProjectedTransfer(result, receiver, source, ArmyExchangeSide::RECEIVER,
				receiverSlot, sourceSlot, requestedCount, receiverHero, sourceHero,
				receiverArmy->needsLastStack(), sourceArmy->needsLastStack());
			if(result.transfers.size() == transferCountBefore)
				continue;
		}
	}

	// Returning an unwanted stack may have opened a slot for a desired creature
	// that could not be assigned before the return was projected.
	for(auto & target : targets)
	{
		if(target.slot.validSlot())
			continue;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(!reservedReceiverSlots[i] && !receiver[i].occupied)
			{
				target.slot = static_cast<SlotID>(i);
				reservedReceiverSlots[i] = true;
				break;
			}
	}

	// Fill existing matching stacks and the previously selected empty slots.
	// Repeated operations are planned against the updated projection; the gateway
	// waits for each callback and verifies its post-state before submitting the next.
	for(const auto & target : targets)
	{
		if(!target.slot.validSlot())
			continue;

		auto & destination = receiver[target.slot.getNum()];
		if(destination.occupied && destination.creature != target.stack.creature)
			continue;

		while((destination.occupied ? destination.count : 0) < target.stack.count)
		{
			bool moved = false;
			for(int sourceIndex = 0; sourceIndex < GameConstants::ARMY_SIZE; ++sourceIndex)
			{
				if(!source[sourceIndex].occupied || source[sourceIndex].creature != target.stack.creature)
					continue;

				const int missing = target.stack.count - (destination.occupied ? destination.count : 0);
				const size_t transferCountBefore = result.transfers.size();
				applyProjectedTransfer(result, receiver, source, ArmyExchangeSide::SOURCE,
					static_cast<SlotID>(sourceIndex), target.slot, missing, receiverHero, sourceHero,
					receiverArmy->needsLastStack(), sourceArmy->needsLastStack());
				if(result.transfers.size() != transferCountBefore)
				{
					moved = true;
					break;
				}
			}

			if(!moved)
				break;
		}
	}

	appendProjectedSlots(receiver, result.receiverSlots);
	appendProjectedSlots(source, result.sourceSlots);
	return result;
}

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

bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination)
{
	if(!source || !destination || source == destination)
		return false;

	// Keep the ordinary slot/merge rule identical to garrisonSwap/moveArmy.
	// Leadership is checked below against the same projected slots that the
	// server builds before applying any move.
	if(!destination->canBeMergedWith(*source, true))
		return false;

	struct ProjectedSlot
	{
		CreatureID creature;
		int count = 0;
		bool occupied = false;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> projected;

	for(const auto & [slot, stack] : destination->Slots())
		projected[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};

	for(const auto & [sourceSlot, sourceStack] : source->Slots())
	{
		int target = -1;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		{
			if(projected[i].occupied && projected[i].creature == sourceStack->getCreatureID())
			{
				target = i;
				break;
			}
		}

		if(target < 0)
		{
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			{
				if(!projected[i].occupied)
				{
					target = i;
					break;
				}
			}
		}

		if(target < 0)
		{
			// moveArmy frees one occupied destination slot by merging another
			// pair, then uses that slot for this source stack. Match that
			// deterministic fallback rather than merely counting creature types.
			int mergeSource = -1;
			int mergeDestination = -1;
			const int preferred = sourceSlot.getNum();
			if(sourceSlot.validSlot() && projected[preferred].occupied)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(j != preferred && projected[j].occupied
						&& projected[j].creature == projected[preferred].creature)
					{
						mergeSource = preferred;
						mergeDestination = j;
						break;
					}
			for(int i = 0; i < GameConstants::ARMY_SIZE && mergeSource < 0; ++i)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(i != j && projected[i].occupied && projected[j].occupied
						&& projected[i].creature == projected[j].creature)
					{
						mergeSource = i;
						mergeDestination = j;
						break;
					}

			if(mergeSource >= 0)
			{
				projected[mergeDestination].count += projected[mergeSource].count;
				if(!canReceiveStack(destination, projected[mergeDestination].creature,
					projected[mergeDestination].count))
					return false;
				projected[mergeSource] = {};
				target = mergeSource;
			}
		}

		if(target < 0)
			return false;
		if(!projected[target].occupied)
			projected[target] = {sourceStack->getCreatureID(), 0, true};
		projected[target].count += sourceStack->getCount();
		if(!canReceiveStack(destination, projected[target].creature, projected[target].count))
			return false;
	}

	return true;
}

bool canSwapGarrisonHero(const CGTownInstance * town)
{
	if(!town)
		return false;
	if(town->getGarrisonHero())
		return true; // hero-to-hero swap or moving a garrison hero out

	const auto * visitingHero = town->getVisitingHero();
	return visitingHero && canMergeArmies(town, visitingHero);
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
