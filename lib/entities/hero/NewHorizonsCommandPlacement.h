/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCommandRules.h"
#include "../creature/NewHorizonsCreatureCategoryRules.h"
#include <span>
#include <vector>

namespace newHorizonsHeroes
{
struct DLL_LINKAGE CommandCreatureTraits
{
	std::optional<newHorizonsCreatures::CreatureCategory> category;
	bool assimilation = false; // Intrinsic creature property supplied by the authoritative caller.
};
enum class CommandPlacementError : uint8_t
{
	NONE, INVALID_SLOT, LOCKED_SLOT, UNKNOWN_CREATURE_TIER, TIER_NOT_PERMITTED
};
/// null command denotes a nonhero/legacy container, not a level-one hero.
DLL_LINKAGE CommandPlacementError commandPlacementEligibility(std::optional<int64_t> command,
	SlotID slot, const CommandCreatureTraits & creature);

struct DLL_LINKAGE CommandPlacementStack
{
	CreatureID creature;
	int64_t count = 0;
	CommandCreatureTraits traits;
	SlotID preferredSlot;
};
struct DLL_LINKAGE CommandPlacementAssignment
{
	size_t inputIndex = 0;
	SlotID slot;
};
/// Complete placement of distinct input stacks, without merging, splitting or
/// mutation. Preserves each identity/count; callers decide existing duplicate and
/// transfer policies before invoking this bounded seven-stack matching search.
/// Preferences are tried first, then ascending slots; failure returns no partial plan.
DLL_LINKAGE std::optional<std::vector<CommandPlacementAssignment>> planCommandPlacement(
	std::optional<int64_t> command, std::span<const CommandPlacementStack> stacks);
}
