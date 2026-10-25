/* ArmyExchangeProjection.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "ArmyFormation.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <utility>

namespace NK2AI::armyFormation
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
	bool sourceNeedsLastStack,
	bool withinReceiver = false)
{
	if(withinReceiver && (sourceSide != ArmyExchangeSide::RECEIVER || sourceSlot == destinationSlot))
		return;
	const bool fromReceiver = sourceSide == ArmyExchangeSide::RECEIVER;
	auto & fromArmy = fromReceiver ? receiver : source;
	auto & toArmy = withinReceiver ? receiver : (fromReceiver ? source : receiver);
	const auto * fromHero = fromReceiver ? receiverHero : sourceHero;
	const auto * toHero = withinReceiver ? receiverHero : (fromReceiver ? sourceHero : receiverHero);
	const bool fromNeedsLastStack = !withinReceiver && (fromReceiver ? receiverNeedsLastStack : sourceNeedsLastStack);
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
	transfer.withinReceiver = withinReceiver;

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


class ExchangeProjectionPlanner
{
	const CCreatureSet * receiverArmy;
	const CCreatureSet * sourceArmy;
	const CGHeroInstance * receiverHero;
	const CGHeroInstance * sourceHero;
	ArmyExchangeProjection result;
	ProjectedArmyState receiver;
	ProjectedArmyState source;
	struct DesiredTarget { DesiredArmyStack stack; SlotID slot; };
	std::vector<DesiredTarget> targets;
	std::set<CreatureID> desiredTypes;
	std::array<bool, GameConstants::ARMY_SIZE> reservedReceiverSlots{};

	void reserveTargets(const std::vector<DesiredArmyStack> & desiredReceiver)
	{
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
	}

	void consolidateReceiver()
	{
		// Only unreserved duplicates may feed an existing desired stack. Every
		// intermediate merge is capacity-checked and retained as a real request.
		for(const auto & target : targets)
		{
			if(!target.slot.validSlot() || !receiverHero
				|| !receiverHero->getLeadershipSlotCapacity(target.stack.creature))
				continue;
			for(int index = 0; index < GameConstants::ARMY_SIZE; ++index)
			{
				if(reservedReceiverSlots[index] || !receiver[index].occupied
					|| receiver[index].creature != target.stack.creature)
					continue;
				const auto & destination = receiver[target.slot.getNum()];
				const int missing = target.stack.count - destination.count;
				if(missing <= 0)
					break;
				applyProjectedTransfer(result, receiver, source, ArmyExchangeSide::RECEIVER,
					static_cast<SlotID>(index), target.slot, missing, receiverHero, sourceHero,
					receiverArmy->needsLastStack(), sourceArmy->needsLastStack(), true);
			}
		}
	}

	void assignEmptyTargets()
	{
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
	}

	void swapUndesired()
	{
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
	}

	void returnUndesired()
	{
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
	}

	void fillTargets()
	{
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
	}


public:
	ExchangeProjectionPlanner(const CCreatureSet * receiverArmy, const CCreatureSet * sourceArmy,
		const CGHeroInstance * receiverHero, const CGHeroInstance * sourceHero)
		: receiverArmy(receiverArmy), sourceArmy(sourceArmy), receiverHero(receiverHero), sourceHero(sourceHero),
		  receiver(makeProjectedArmy(receiverArmy)), source(makeProjectedArmy(sourceArmy)) {}

	ArmyExchangeProjection run(const std::vector<DesiredArmyStack> & desiredReceiver)
	{
		if(receiverArmy && sourceArmy && receiverArmy != sourceArmy)
		{
			reserveTargets(desiredReceiver);
			consolidateReceiver();
			assignEmptyTargets();
			swapUndesired();
			returnUndesired();
			assignEmptyTargets();
			fillTargets();
		}
		appendProjectedSlots(receiver, result.receiverSlots);
		appendProjectedSlots(source, result.sourceSlots);
		return std::move(result);
	}
};
}

ArmyExchangeProjection projectArmyExchange(const CCreatureSet * receiverArmy, const CCreatureSet * sourceArmy,
	const CGHeroInstance * receiverHero, const CGHeroInstance * sourceHero,
	const std::vector<DesiredArmyStack> & desiredReceiver)
{
	return ExchangeProjectionPlanner(receiverArmy, sourceArmy, receiverHero, sourceHero).run(desiredReceiver);
}
}
