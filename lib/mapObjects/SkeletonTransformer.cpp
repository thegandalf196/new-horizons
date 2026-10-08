/*
 * SkeletonTransformer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SkeletonTransformer.h"

#include "CGHeroInstance.h"
#include "army/CArmedInstance.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"

#include <algorithm>
#include <array>
#include <limits>

namespace newHorizonsSkeletonTransformer
{
Plan plan(const CArmedInstance & army, std::span<const SlotID> selectedSlots,
	const JsonNode & savedCapabilityRules)
{
	Plan result;
	for(const auto & [slot, stack] : army.Slots())
	{
		if(stack)
			result.projectedArmy.push_back({slot, stack->getCreatureID(), stack->getCount()});
	}
	const auto percent = newHorizonsHeroes::capabilitySkeletonTransformerHealthPercent(savedCapabilityRules);
	if(!percent)
	{
		result.status = PlanStatus::LEGACY_RULES;
		return result;
	}
	if(selectedSlots.empty())
		return result;

	std::array<bool, GameConstants::ARMY_SIZE> selected{};
	for(const auto slot : selectedSlots)
	{
		if(!slot.validSlot() || !army.hasStackAtSlot(slot) || !army.getStackPtr(slot))
		{
			result.status = PlanStatus::INVALID_SLOT;
			return result;
		}
		if(selected[slot.getNum()])
		{
			result.status = PlanStatus::DUPLICATE_SLOT;
			return result;
		}
		selected[slot.getNum()] = true;
	}
	for(const auto & [slot, stack] : army.Slots())
	{
		if(!slot.validSlot() || !stack || !stack->getCreature() || stack->getCount() <= 0)
		{
			result.status = PlanStatus::INVALID_COUNT;
			return result;
		}
		if(!selected[slot.getNum()])
			continue;

		// Sacrificed adventure HP includes the live stack's attached bonuses.
		// There is no battle wound state in an adventure army.
		const int64_t health = stack->getMaxHealth();
		const int64_t count = stack->getCount();
		if(health <= 0)
		{
			result.status = PlanStatus::INVALID_COUNT;
			return result;
		}
		if(count > (std::numeric_limits<int64_t>::max() - result.sacrificedHitPoints) / health)
		{
			result.status = PlanStatus::HP_OVERFLOW;
			return result;
		}
		result.sacrificedHitPoints += count * health;
	}
	// Divide before multiplying to keep even large valid aggregate HP safe.
	result.convertedHitPoints = (result.sacrificedHitPoints / 100) * *percent
		+ ((result.sacrificedHitPoints % 100) * *percent) / 100;
	// Output HP comes from the live creature definition, without attaching a
	// hypothetical Skeleton stack to the hero's bonus graph.
	const auto * skeleton = CreatureID(CreatureID::SKELETON).toCreature();
	if(!skeleton || skeleton->getMaxHealth() == 0)
	{
		result.status = PlanStatus::INVALID_COUNT;
		return result;
	}
	result.skeletonCount = result.convertedHitPoints / skeleton->getMaxHealth();
	if(result.skeletonCount == 0)
	{
		result.status = PlanStatus::ZERO_OUTPUT;
		return result;
	}

	int64_t maximum = std::numeric_limits<int>::max();
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(&army))
	{
		if(const auto capacity = hero->getLeadershipSlotCapacity(CreatureID::SKELETON))
			maximum = std::min<int64_t>(maximum, capacity->maximum);
	}
	if(maximum <= 0)
	{
		result.status = PlanStatus::LEADERSHIP_LIMIT;
		return result;
	}

	std::vector<ProjectedStack> projected;
	for(const auto & stack : result.projectedArmy)
		if(!selected[stack.slot.getNum()])
			projected.push_back(stack);
	std::vector<OutputStack> outputs;
	int64_t remaining = result.skeletonCount;
	for(auto & stack : projected)
	{
		if(stack.creature != CreatureID::SKELETON || stack.count >= maximum)
			continue;
		const int64_t added = std::min(remaining, maximum - stack.count);
		if(added > 0)
		{
			stack.count += added;
			outputs.push_back({stack.slot, stack.count});
			remaining -= added;
		}
	}
	// Stable ascending slots make preview and server output independent of the
	// order in which source icons were selected.
	for(int index = 0; index < GameConstants::ARMY_SIZE && remaining > 0; ++index)
	{
		if(!selected[index])
			continue;
		const int64_t count = std::min(remaining, maximum);
		const SlotID slot(index);
		projected.push_back({slot, CreatureID::SKELETON, count});
		outputs.push_back({slot, count});
		remaining -= count;
	}
	if(remaining > 0)
	{
		result.status = PlanStatus::OUTPUT_CAPACITY;
		return result;
	}
	if(projected.empty() && army.needsLastStack())
	{
		result.status = PlanStatus::LAST_STACK;
		return result;
	}
	std::sort(projected.begin(), projected.end(), [](const auto & lhs, const auto & rhs)
	{
		return lhs.slot < rhs.slot;
	});
	result.outputs = std::move(outputs);
	result.projectedArmy = std::move(projected);
	result.status = PlanStatus::READY;
	return result;
}
}
