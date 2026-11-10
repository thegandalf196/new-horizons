/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../Helpers/ArmyFormation.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"

namespace NK2AI
{
std::vector<SlotInfo> ArmyManager::projectArmyAdmission(const CCreatureSet * destination, const CCreatureSet * source,
	const CGHeroInstance * receiver, const CGHeroInstance * sourceHero, const std::vector<SlotInfo> & desired,
	armyFormation::ArmyExchangeProjection * plan) const
{
	std::vector<armyFormation::DesiredArmyStack> stacks;
	int free = GameConstants::ARMY_SIZE;
	for(const auto & row : desired)
	{
		if(receiver && receiver->getArmyCommand())
		{
			stacks.push_back({row.creature->getId(), row.count});
			continue;
		}
		const auto historical = receiver ? receiver->getLeadershipSlotCapacity(row.creature->getId()) : std::nullopt;
		const int maximum = historical ? historical->maximum : row.count;
		for(int remaining = row.count; remaining > 0 && free > 0 && maximum > 0; --free)
		{
			const int count = std::min(remaining, maximum);
			stacks.push_back({row.creature->getId(), count});
			remaining -= count;
		}
	}
	const auto projection = armyFormation::projectArmyExchange(destination, source, receiver, sourceHero, stacks);
	if(plan) *plan = projection; // Retain this exact executable plan behind the valuation.
	std::vector<SlotInfo> result;
	for(const auto & row : projection.receiverSlots)
	{
		const auto * creature = row.creature.toCreature();
		result.push_back({creature, row.count, evaluateStackPower(creature, row.count)});
	}
	return result;
}
}
