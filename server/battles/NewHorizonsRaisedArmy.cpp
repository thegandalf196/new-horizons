/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRaisedArmy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CStack.h"
namespace newHorizonsNecromancy
{
std::vector<std::pair<CreatureID, int64_t>> raisedOutputs(const NecromancyResult & result)
{
	return {{result.skeletonCreature.hasValue() ? result.skeletonCreature
			: CreatureID(CreatureID::decode("core:skeleton")), result.skeletonsRaised},
		{CreatureID(CreatureID::decode("core:zombie")), result.zombiesRaised},
		{CreatureID(CreatureID::decode("core:wight")), result.wightsRaised},
		{CreatureID(CreatureID::decode("core:boneDragon")), result.boneDragonsRaised}};
}
bool validRaisedArmyOutputs(const std::vector<std::pair<CreatureID, int64_t>> & outputs)
{
	for(const auto & [creature, count] : outputs)
		if(count < 0 || (count > 0 && (!creature.hasValue() || !creature.toCreature())))
			return false;
	return true;
}

bool validRaisedArmyState(const CArmedInstance & army)
{
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(const auto * stack = army.getStackPtr(SlotID(i)))
		{
			if(stack->getCount() < 0 || !stack->getCreatureID().hasValue()
				|| !stack->getCreatureID().toCreature())
				return false;
		}
	}
	return true;
}

// Resolve every output against one projected army before emitting any packs.
// A matching stack may be full even though another slot can admit the reward.
std::optional<RaisedArmyPlan> planRaisedArmy(const CArmedInstance & army,
	const std::vector<std::pair<CreatureID, int64_t>> & outputs)
{
	struct ProjectedSlot
	{
		CreatureID creature = CreatureID::NONE;
		int64_t count = 0;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> slots;
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		if(const auto * stack = army.getStackPtr(SlotID(i)))
		{
			if(stack->getCount() < 0)
				return std::nullopt;
			slots[i] = {stack->getCreatureID(), stack->getCount()};
		}

	RaisedArmyPlan plan;
	for(const auto & [creature, count] : outputs)
	{
		if(count < 0 || (count > 0 && !creature.hasValue()))
			return std::nullopt;
		if(count == 0)
			continue;
		int64_t capacity = std::numeric_limits<TQuantity>::max();
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(&army))
		{
			if(const auto leadership = hero->getLeadershipSlotCapacity(creature))
				capacity = std::min<int64_t>(capacity, leadership->maximum);
		}
		int64_t remaining = count;
		// Fill every matching stack first, then reserve distinct empty slots.
		for(const bool empty : {false, true})
			for(int i = 0; i < GameConstants::ARMY_SIZE && remaining > 0; ++i)
			{
				auto & slot = slots[i];
				if(empty ? slot.creature.hasValue() : slot.creature != creature)
					continue;
				const int64_t amount = std::min(remaining, std::max<int64_t>(0, capacity - slot.count));
				if(amount == 0)
					continue;
				plan.push_back({SlotID(i), creature, static_cast<TQuantity>(amount)});
				slot.creature = creature;
				slot.count += amount;
				remaining -= amount;
			}
		if(remaining > 0)
			return std::nullopt;
	}
	return plan;
}

}
