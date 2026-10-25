/*
 * NewHorizonsLeadership.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsLeadership.h"

#include "../../mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace newHorizonsHeroes
{
LeadershipCapacity leadershipCapacity(int classBase, int classPerLevel,
	int level, int skillBonusPercent, uint64_t used, int minimumMovementPercent)
{
	constexpr int maximumProfileValue = 1000000;
	constexpr int maximumSkillBonusPercent = 1000;
	if(classBase <= 0 || classBase > maximumProfileValue
		|| classPerLevel < 0 || classPerLevel > maximumProfileValue
		|| level <= 0 || level > maximumProfileValue
		|| skillBonusPercent < 0 || skillBonusPercent > maximumSkillBonusPercent
		|| minimumMovementPercent <= 0 || minimumMovementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership capacity inputs");

	const int64_t base = classBase + int64_t(level - 1) * classPerLevel;
	const int64_t capacity = base * (100 + skillBonusPercent) / 100;
	int movementPercent = 100;
	if(used > static_cast<uint64_t>(capacity))
		movementPercent = std::max(minimumMovementPercent,
			static_cast<int>(static_cast<uint64_t>(capacity) * 100 / used));
	return {capacity, used, movementPercent};
}

int leadershipMovement(int unscaledMovement, int movementPercent)
{
	if(unscaledMovement < 0 || movementPercent <= 0 || movementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership movement inputs");
	return static_cast<int>(int64_t(unscaledMovement) * movementPercent / 100);
}

bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination)
{
	return canMergeArmies(source, destination, dynamic_cast<const CGHeroInstance *>(destination));
}

bool canMergeArmies(const CCreatureSet * source, const CCreatureSet * destination,
	const CGHeroInstance * hero)
{
	if(!source || !destination || source == destination)
		return false;

	// Keep the ordinary slot/merge rule identical to garrisonSwap/moveArmy.
	// Leadership is checked below against the same projected slots that the
	// server builds before applying any move.
	if(!destination->canBeMergedWith(*source, true))
		return false;

	const auto acceptsProjectedStack = [hero](CreatureID creature, int64_t count)
	{
		const auto capacity = hero ? hero->getLeadershipSlotCapacity(creature) : std::nullopt;
		return !capacity || (count >= 0 && count <= capacity->maximum);
	};

	struct ProjectedSlot
	{
		CreatureID creature;
		int64_t count = 0;
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
				if(!acceptsProjectedStack(projected[mergeDestination].creature,
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
		if(!acceptsProjectedStack(projected[target].creature, projected[target].count))
			return false;
	}

	return true;
}

SlotID recruitmentSlot(const CArmedInstance * army, CreatureID creature, int64_t requestedAmount)
{
	if(!army || !creature.toCreature())
		return SlotID();

	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	const auto capacity = hero && requestedAmount > 0
		? hero->getLeadershipSlotCapacity(creature) : std::nullopt;
	const std::optional<int64_t> maximum = capacity
		? std::optional<int64_t>(capacity->maximum) : std::nullopt;
	std::vector<RecruitmentSlot> slots;
	slots.reserve(GameConstants::ARMY_SIZE);
	for(const auto & [slot, stack] : army->Slots())
		slots.push_back({slot, stack->getCreatureID(), stack->getCount()});
	return recruitmentSlot(slots, creature, requestedAmount, maximum);
}

SlotID recruitmentSlot(std::span<const RecruitmentSlot> slots, CreatureID creature,
	int64_t requestedAmount, std::optional<int64_t> maximum)
{
	if(!creature.toCreature())
		return SlotID();

	std::array<const RecruitmentSlot *, GameConstants::ARMY_SIZE> occupied{};
	SlotID legacySlot;
	for(const auto & entry : slots)
	{
		if(!entry.slot.validSlot() || occupied[entry.slot.getNum()])
			throw std::invalid_argument("Recruitment slots must be valid and unique");
		occupied[entry.slot.getNum()] = &entry;
		if(entry.creature == creature && (!legacySlot.validSlot() || entry.slot < legacySlot))
			legacySlot = entry.slot;
	}
	for(int i = 0; i < GameConstants::ARMY_SIZE && !legacySlot.validSlot(); ++i)
		if(!occupied[i])
			legacySlot = SlotID(i);
	if(!maximum || requestedAmount <= 0)
		return legacySlot;

	const int64_t maximumStackCount = std::max<int64_t>(0, *maximum);
	SlotID bestSlot;
	int64_t bestHeadroom = -1;

	// Matching stacks take priority for a whole request. Match getCreatureSlots'
	// exclusion of unresolved/nonpositive counts, retaining physical slot order.
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		const auto * entry = occupied[i];
		if(!entry || entry->creature != creature || entry->count < 1)
			continue;
		const int64_t headroom = std::max<int64_t>(0, maximumStackCount - entry->count);
		if(headroom >= requestedAmount)
			return SlotID(i);
		if(headroom > bestHeadroom)
		{
			bestSlot = SlotID(i);
			bestHeadroom = headroom;
		}
	}

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(occupied[i])
			continue;
		if(maximumStackCount >= requestedAmount)
			return SlotID(i);
		if(maximumStackCount > bestHeadroom)
		{
			bestSlot = SlotID(i);
			bestHeadroom = maximumStackCount;
		}
	}

	// Retain a full matching fallback so authoritative validation rejects it.
	return bestHeadroom >= 0 ? bestSlot : legacySlot;
}
}
