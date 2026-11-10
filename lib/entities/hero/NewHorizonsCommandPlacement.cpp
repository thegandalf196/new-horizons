/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandPlacement.h"
#include "../../GameConstants.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
CommandPlacementError commandPlacementEligibility(std::optional<int64_t> command,
	SlotID slot, const CommandCreatureTraits & creature)
{
	if(!slot.validSlot())
		return CommandPlacementError::INVALID_SLOT;
	if(!command)
		return CommandPlacementError::NONE;
	const auto view = commandSlotView(*command, slot);
	if(!view.unlocked)
		return CommandPlacementError::LOCKED_SLOT;
	if(creature.assimilation)
		return CommandPlacementError::NONE;
	if(!creature.category)
		return CommandPlacementError::UNKNOWN_CREATURE_TIER;
	using Category = newHorizonsCreatures::CreatureCategory;
	if(*creature.category != Category::CORE && *creature.category != Category::ELITE && *creature.category != Category::CHAMPION)
		return CommandPlacementError::UNKNOWN_CREATURE_TIER;
	const bool permitted = *creature.category == Category::CORE
		|| view.grade == CommandSlotGrade::CHAMPION
		|| (*creature.category == Category::ELITE && view.grade == CommandSlotGrade::ELITE);
	return permitted ? CommandPlacementError::NONE : CommandPlacementError::TIER_NOT_PERMITTED;
}

namespace
{
bool place(size_t index, std::span<const CommandPlacementStack> stacks,
	const std::vector<std::vector<SlotID>> & candidates, std::array<bool, GameConstants::ARMY_SIZE> & used,
	std::vector<CommandPlacementAssignment> & plan)
{
	if(index == stacks.size())
		return true;
	for(const auto slot : candidates[index])
	{
		if(used[slot.getNum()])
			continue;
		used[slot.getNum()] = true;
		plan.push_back({index, slot});
		if(place(index + 1, stacks, candidates, used, plan))
			return true;
		plan.pop_back();
		used[slot.getNum()] = false;
	}
	return false;
}
}

std::optional<std::vector<CommandPlacementAssignment>> planCommandPlacement(
	std::optional<int64_t> command, std::span<const CommandPlacementStack> stacks)
{
	if(command && *command < 1)
		throw std::invalid_argument("Invalid Command placement context");
	if(stacks.size() > GameConstants::ARMY_SIZE)
		return std::nullopt;
	std::vector<std::vector<SlotID>> candidates(stacks.size());
	for(size_t i = 0; i < stacks.size(); ++i)
	{
		const auto & stack = stacks[i];
		if(!stack.creature.hasValue() || stack.count < 1 || stack.count > std::numeric_limits<TQuantity>::max())
			return std::nullopt;
		const auto trySlot = [&](SlotID slot)
		{
			if(commandPlacementEligibility(command, slot, stack.traits) == CommandPlacementError::NONE)
				candidates[i].push_back(slot);
		};
		if(stack.preferredSlot.validSlot())
			trySlot(stack.preferredSlot);
		for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
			if(SlotID(slot) != stack.preferredSlot)
				trySlot(SlotID(slot));
	}
	std::array<bool, GameConstants::ARMY_SIZE> used{};
	std::vector<CommandPlacementAssignment> plan;
	if(!place(0, stacks, candidates, used, plan))
		return std::nullopt;
	return plan;
}
}
