/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandArmyAdmission.h"
#include "CGameHandler.h"
#include "../lib/entities/hero/NewHorizonsLeadership.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/networkPacks/StackLocation.h"
#include <limits>
#include <stdexcept>

namespace commandArmyAdmission
{
bool validateSlot(CGameHandler & handler, const CArmedInstance * army, SlotID slot,
	CreatureID creature, int64_t count, const char *)
{
	if(!army || !slot.validSlot() || !creature.hasValue() || count < 0 || count > std::numeric_limits<TQuantity>::max())
	{
		handler.complain("Invalid army slot or creature count.");
		return false;
	}
	// All 7 army slots are unrestricted; creature stacks of any positive count are legal.
	// Offensive damage scales via Leadership Efficiency; stacks are not rejected.
	return true;
}

void rearrange(CGameHandler & handler, const CArmedInstance & army,
	const newHorizonsHeroes::CommandArmyPlan & plan)
{
	for(const auto & move : plan.moves)
	{
		if(!move.withinDestination) continue;
		const StackLocation from(army.id, move.source), to(army.id, move.destination);
		const bool accepted = move.swap ? handler.swapStacks(from, to) : handler.moveStack(from, to, move.count);
		if(!accepted) throw std::runtime_error("Preflighted Command army rearrangement was rejected");
	}
}

bool transfer(CGameHandler & handler, const CArmedInstance & source, const CGHeroInstance & destination,
	bool allowMerging)
{
	if(&source == &destination || (source.needsLastStack() && source.stacksCount() > 0)) return false;
	const auto plan = newHorizonsHeroes::planCommandArmyAddition(destination, destination, source, allowMerging);
	if(!plan)
	{
		handler.complain("The complete army cannot fit the hero's unlocked tier slots.");
		return false;
	}
	rearrange(handler, destination, *plan);
	for(const auto & move : plan->moves)
		if(!move.withinDestination && !handler.moveStack(StackLocation(source.id, move.source),
			StackLocation(destination.id, move.destination), move.count))
			throw std::runtime_error("Preflighted Command army transfer was rejected");
	return true;
}

bool give(CGameHandler & handler, const CGHeroInstance & destination, const CCreatureSet & incoming)
{
	const auto plan = newHorizonsHeroes::planCommandArmyAddition(destination, destination, incoming);
	if(!plan)
	{
		handler.complain("The creature reward cannot fit the hero's unlocked tier slots. No creatures were granted.");
		return false;
	}
	rearrange(handler, destination, *plan);
	for(const auto & move : plan->moves)
		if(!move.withinDestination && !handler.addToSlot(StackLocation(destination.id, move.destination),
			incoming.getCreature(move.source), move.count))
			throw std::runtime_error("Preflighted Command creature reward was rejected");
	return true;
}
}
