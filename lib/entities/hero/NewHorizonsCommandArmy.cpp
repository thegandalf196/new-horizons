/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandArmy.h"
#include "NewHorizonsLeadership.h"
#include "../creature/NewHorizonsAssimilation.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <array>
#include <limits>

namespace newHorizonsHeroes
{
CommandCreatureTraits commandArmyCreatureTraits(const CGHeroInstance & hero, CreatureID creature)
{
	return hero.getCommandCreatureTraits(creature);
}

CommandPlacementError commandArmySlotEligibility(const CGHeroInstance * hero, SlotID slot, CreatureID creature)
{
	if(!hero || !hero->getArmyCommand())
		return commandPlacementEligibility(std::nullopt, slot, {});
	return commandPlacementEligibility(hero->getArmyCommand()->effective(), slot, commandArmyCreatureTraits(*hero, creature));
}

bool armySlotAccepts(const CArmedInstance * army, SlotID slot, CreatureID creature, int64_t count)
{
	if(!army || !slot.validSlot() || !creature.hasValue() || count < 0 || count > std::numeric_limits<TQuantity>::max())
		return false;
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(hero && hero->getArmyCommand())
		return commandArmySlotEligibility(hero, slot, creature) == CommandPlacementError::NONE;
	return true;
}

std::string commandPlacementMessage(CommandPlacementError error)
{
	switch(error)
	{
	case CommandPlacementError::NONE: return {};
	case CommandPlacementError::INVALID_SLOT: return "Invalid army slot.";
	case CommandPlacementError::LOCKED_SLOT: return "This army slot is locked. Increase Command to unlock it.";
	case CommandPlacementError::UNKNOWN_CREATURE_TIER: return "This creature has no captured army-tier permission.";
	case CommandPlacementError::TIER_NOT_PERMITTED: return "This slot does not permit this creature's tier. Use an unlocked higher-grade slot.";
	}
	return "Invalid army placement.";
}

std::vector<RecruitmentSlot> commandArmySlots(const CCreatureSet & army)
{
	std::vector<RecruitmentSlot> result;
	for(const auto & [slot, stack] : army.Slots())
		result.push_back({slot, stack->getCreatureID(), stack->getCount()});
	return result;
}

SlotID commandRecruitmentSlot(const CGHeroInstance & hero, std::span<const RecruitmentSlot> slots,
	CreatureID creature, int64_t amount)
{
	if(amount <= 0 || amount > std::numeric_limits<TQuantity>::max()) return SlotID();
	std::array<const RecruitmentSlot *, GameConstants::ARMY_SIZE> occupied{};
	for(const auto & row : slots)
	{
		if(!row.slot.validSlot() || occupied[row.slot.getNum()]) return SlotID();
		occupied[row.slot.getNum()] = &row;
	}
	for(bool matching : {true, false})
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		{
			const auto * row = occupied[i];
			if(matching ? !row || row->creature != creature : row != nullptr) continue;
			if((!row || row->count <= std::numeric_limits<TQuantity>::max() - amount)
				&& commandArmySlotEligibility(&hero, SlotID(i), creature) == CommandPlacementError::NONE)
				return SlotID(i);
		}
	return SlotID();
}

namespace
{
struct Group
{
	CommandPlacementStack stack;
	std::vector<RecruitmentSlot> origins;
	bool existing = false;
};

bool relocate(const CGHeroInstance & hero, std::vector<Group> & groups,
	const std::vector<CommandPlacementAssignment> & assignment, CommandArmyPlan & plan)
{
	std::array<int, GameConstants::ARMY_SIZE> occupants;
	occupants.fill(-1);
	std::vector<SlotID> current(groups.size()), target(groups.size());
	for(const auto & row : assignment) target[row.inputIndex] = row.slot;
	for(size_t i = 0; i < groups.size(); ++i)
		if(groups[i].existing)
		{
			current[i] = groups[i].stack.preferredSlot;
			occupants[current[i].getNum()] = static_cast<int>(i);
		}
	for(size_t pass = 0; pass < groups.size() * groups.size() + 1; ++pass)
	{
		bool pending = false, progressed = false;
		for(size_t i = 0; i < groups.size(); ++i)
		{
			if(!current[i].validSlot() || current[i] == target[i]) continue;
			pending = true;
			const int other = occupants[target[i].getNum()];
			if(other >= 0 && commandArmySlotEligibility(&hero, current[i], groups[other].stack.creature) != CommandPlacementError::NONE)
				continue;
			int64_t existingCount = groups[i].stack.count;
			for(const auto & origin : groups[i].origins) existingCount -= origin.count;
			plan.moves.push_back({true, current[i], target[i], static_cast<int32_t>(existingCount), other >= 0});
			occupants[current[i].getNum()] = other;
			occupants[target[i].getNum()] = static_cast<int>(i);
			if(other >= 0) current[other] = current[i];
			current[i] = target[i];
			progressed = true;
		}
		if(!pending) return true;
		if(!progressed) return false;
	}
	return false;
}
}

std::optional<CommandArmyPlan> planCommandArmyAddition(const CGHeroInstance & hero,
	std::span<const RecruitmentSlot> destination, std::span<const RecruitmentSlot> incoming, bool allowMerging)
{
	const auto command = hero.getArmyCommand();
	if(!command) return std::nullopt;
	CommandArmyPlan plan;
	std::vector<Group> groups;
	std::array<bool, GameConstants::ARMY_SIZE> occupied{};
	for(const auto & row : destination)
	{
		if(!row.slot.validSlot() || occupied[row.slot.getNum()] || !row.creature.hasValue()
			|| row.count < 1 || row.count > std::numeric_limits<TQuantity>::max()) return std::nullopt;
		occupied[row.slot.getNum()] = true;
		groups.push_back({{row.creature, row.count, commandArmyCreatureTraits(hero, row.creature), row.slot}, {}, true});
	}
	occupied.fill(false);
	for(const auto & row : incoming)
	{
		if(!row.slot.validSlot() || occupied[row.slot.getNum()] || !row.creature.hasValue()
			|| row.count < 1 || row.count > std::numeric_limits<TQuantity>::max()) return std::nullopt;
		occupied[row.slot.getNum()] = true;
		auto match = allowMerging ? std::ranges::find_if(groups, [&](const Group & group)
		{
			return group.stack.creature == row.creature
				&& group.stack.count <= std::numeric_limits<TQuantity>::max() - row.count;
		}) : groups.end();
		if(match == groups.end())
			groups.push_back({{row.creature, row.count, commandArmyCreatureTraits(hero, row.creature), SlotID()}, {row}, false});
		else
		{
			match->stack.count += row.count;
			match->origins.push_back(row);
		}
	}
	std::optional<std::vector<CommandPlacementAssignment>> assignment;
	for(;;)
	{
		std::vector<CommandPlacementStack> stacks;
		for(const auto & group : groups) stacks.push_back(group.stack);
		assignment = planCommandPlacement(command->effective(), stacks);
		if(assignment) break;
		// Preserve existing duplicate stacks unless consolidation is needed.
		bool merged = false;
		if(allowMerging)
			for(size_t i = 0; i < groups.size() && !merged; ++i)
				for(size_t j = i + 1; j < groups.size(); ++j)
					if(groups[i].existing && groups[j].existing && groups[i].stack.creature == groups[j].stack.creature
						&& groups[i].stack.count <= std::numeric_limits<TQuantity>::max() - groups[j].stack.count)
					{
						const auto existingCount = groups[j].stack.count;
						int64_t incomingCount = 0;
						for(const auto & origin : groups[j].origins) incomingCount += origin.count;
						plan.moves.push_back({true, groups[j].stack.preferredSlot, groups[i].stack.preferredSlot,
							static_cast<int32_t>(existingCount - incomingCount), false});
						groups[i].stack.count += existingCount;
						groups[i].origins.insert(groups[i].origins.end(), groups[j].origins.begin(), groups[j].origins.end());
						groups.erase(groups.begin() + j);
						merged = true;
						break;
					}
		if(!merged) return std::nullopt;
	}
	if(!relocate(hero, groups, *assignment, plan)) return std::nullopt;
	for(const auto & row : *assignment)
	{
		const auto & group = groups[row.inputIndex];
		for(const auto & origin : group.origins)
			plan.moves.push_back({false, origin.slot, row.slot, static_cast<int32_t>(origin.count), false});
		plan.slots.push_back({row.slot, group.stack.creature, group.stack.count});
	}
	return plan;
}

std::optional<CommandArmyPlan> planCommandArmyAddition(const CGHeroInstance & hero,
	const CCreatureSet & destination, const CCreatureSet & incoming, bool allowMerging)
{
	return planCommandArmyAddition(hero, commandArmySlots(destination), commandArmySlots(incoming), allowMerging);
}

std::optional<CommandArmyPlan> planCommandRecruitment(const CGHeroInstance & hero,
	std::span<const RecruitmentSlot> destination, CreatureID creature, int64_t count)
{
	const std::array incoming{RecruitmentSlot{SlotID(0), creature, count}};
	return planCommandArmyAddition(hero, destination, incoming);
}

SlotID commandRecruitmentDestination(const CommandArmyPlan & plan)
{
	for(const auto & move : plan.moves)
		if(!move.withinDestination) return move.destination;
	return SlotID();
}
}
