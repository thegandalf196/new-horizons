/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCommandPlacement.h"
#include <span>
#include <string>
class CCreatureSet;
class CArmedInstance;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct DLL_LINKAGE RecruitmentSlot
{
	SlotID slot;
	CreatureID creature;
	int64_t count;
};
/// Matcher permission classes; specialist permission is represented as Core here
/// only. getCommandCreatureTraits and world category queries retain actual tier.
DLL_LINKAGE CommandCreatureTraits commandArmyCreatureTraits(const CGHeroInstance & hero, CreatureID creature);
DLL_LINKAGE CommandPlacementError commandArmySlotEligibility(const CGHeroInstance * hero, SlotID slot, CreatureID creature);
DLL_LINKAGE bool armySlotAccepts(const CArmedInstance * army, SlotID slot, CreatureID creature, int64_t count);
DLL_LINKAGE std::string commandPlacementMessage(CommandPlacementError error);

struct DLL_LINKAGE CommandArmyMove
{
	bool withinDestination = false;
	SlotID source, destination;
	int32_t count = 0;
	bool swap = false;
};
struct DLL_LINKAGE CommandArmyPlan
{
	std::vector<CommandArmyMove> moves;
	std::vector<RecruitmentSlot> slots;
};
/// All-or-nothing plan, including legal destination rearrangement. Source slots
/// denote a separate container; no mutation, troop deletion or skill/RNG changes.
DLL_LINKAGE std::optional<CommandArmyPlan> planCommandArmyAddition(const CGHeroInstance & hero,
	std::span<const RecruitmentSlot> destination, std::span<const RecruitmentSlot> incoming, bool allowMerging = true);
DLL_LINKAGE std::optional<CommandArmyPlan> planCommandArmyAddition(const CGHeroInstance & hero,
	const CCreatureSet & destination, const CCreatureSet & incoming, bool allowMerging = true);
DLL_LINKAGE std::vector<RecruitmentSlot> commandArmySlots(const CCreatureSet & army);
DLL_LINKAGE std::optional<CommandArmyPlan> planCommandRecruitment(const CGHeroInstance & hero,
	std::span<const RecruitmentSlot> destination, CreatureID creature, int64_t count);
DLL_LINKAGE SlotID commandRecruitmentDestination(const CommandArmyPlan & plan);
/// Single-request direct destination. Does not rearrange; callers needing that
/// capability must apply the complete plan, never just its final slot number.
DLL_LINKAGE SlotID commandRecruitmentSlot(const CGHeroInstance & hero,
	std::span<const RecruitmentSlot> slots, CreatureID creature, int64_t amount);
}
