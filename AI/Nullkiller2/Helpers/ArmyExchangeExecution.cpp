/*
 * ArmyExchangeExecution.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "ArmyExchangeExecution.h"
#include "ArmyFormation.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"

namespace NK2AI::armyFormation
{

void executeArmyExchange(const std::shared_ptr<CCallback> & cc,
	const CArmedInstance * destinationArmy, const CArmedInstance * source,
	const ArmyExchangeProjection & projection)
{
	const bool previousWaitTillRealize = cc->waitTillRealize;
	cc->waitTillRealize = true;
	auto restoreWaitTillRealize = vstd::makeScopeGuard([callback = cc, previousWaitTillRealize]()
	{
		callback->waitTillRealize = previousWaitTillRealize;
	});

	auto stackMatches = [](const CArmedInstance * army, SlotID slot, CreatureID creature, int count)
	{
		const auto * stack = army->getStackPtr(slot);
		if(count == 0)
			return stack == nullptr;
		return stack && stack->getCreatureID() == creature && stack->getCount() == count;
	};

	for(const auto & transfer : projection.transfers)
	{
		const bool fromReceiver = transfer.sourceSide == armyFormation::ArmyExchangeSide::RECEIVER;
		const auto * fromArmy = fromReceiver ? destinationArmy : source;
		if(transfer.withinReceiver && (!fromReceiver || transfer.sourceSlot == transfer.destinationSlot))
		{
			logAi->warn("Stopping projected army exchange before an invalid receiver consolidation");
			break;
		}
		const auto * toArmy = transfer.withinReceiver ? destinationArmy : (fromReceiver ? source : destinationArmy);
		if(!stackMatches(fromArmy, transfer.sourceSlot, transfer.expectedSourceCreature, transfer.expectedSourceCount)
			|| !stackMatches(toArmy, transfer.destinationSlot,
				transfer.expectedDestinationCreature, transfer.expectedDestinationCount))
		{
			logAi->warn("Stopping projected army exchange after stack state changed at slots %d and %d",
				transfer.sourceSlot.getNum(), transfer.destinationSlot.getNum());
			break;
		}

		if(transfer.swapsStacks || transfer.transferCount == transfer.expectedSourceCount)
		{
			cc->mergeOrSwapStacks(fromArmy, toArmy, transfer.sourceSlot, transfer.destinationSlot);
		}
		else
		{
			const auto * destinationStack = toArmy->getStackPtr(transfer.destinationSlot);
			if(destinationStack && destinationStack->getCreatureID() != transfer.expectedSourceCreature)
			{
				logAi->warn("Stopping projected army exchange before partial transfer to an occupied different stack");
				break;
			}
			cc->splitStack(fromArmy, toArmy, transfer.sourceSlot, transfer.destinationSlot,
				transfer.resultingDestinationCount);
		}

		const int remainingSourceCount = transfer.expectedSourceCount - transfer.transferCount;
		const bool postStateMatches = transfer.swapsStacks
			? stackMatches(fromArmy, transfer.sourceSlot,
				transfer.expectedDestinationCreature, transfer.expectedDestinationCount)
				&& stackMatches(toArmy, transfer.destinationSlot,
					transfer.expectedSourceCreature, transfer.expectedSourceCount)
			: stackMatches(fromArmy, transfer.sourceSlot,
				remainingSourceCount > 0 ? transfer.expectedSourceCreature : CreatureID::NONE,
				remainingSourceCount)
				&& stackMatches(toArmy, transfer.destinationSlot,
					transfer.expectedSourceCreature, transfer.resultingDestinationCount);
		if(!postStateMatches)
		{
			logAi->warn("Stopping projected army exchange because the server did not apply the planned stack change");
			break;
		}
	}
}

}
