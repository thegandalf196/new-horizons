/*
 * NewHorizonsCapabilityRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NewHorizonsLeadership.h"
#include "../../constants/EntityIdentifiers.h"
#include "../../json/JsonNode.h"

namespace newHorizonsHeroes
{
constexpr int CAPABILITY_RULESET_VERSION = 3;

struct DLL_LINKAGE SiegeCapabilities
{
	// Legacy Artillery/Ballistics/First Aid projection. These remain populated
	// for old snapshots and are intentionally not consulted by ruleset v3.
	int artilleryRank = 0;
	int ballisticsRank = 0;
	int firstAidRank = 0;
	int ballistaDamageMultiplier = 1;
	int ballistaControlChance = 0;
	int catapultControlChance = 0;
	int firstAidControlChance = 0;
	// Canonical New Horizons War Machines projection. The zero/default values
	// preserve source compatibility for callers that only understand legacy
	// capability fields; ruleset v3 fills these from its saved rank tables.
	int warMachinesRank = 0;
	int siegeRating = 0;
	int ballistaDamage = 50;
	int catapultStructuralDamage = 100;
	int firstAidHealing = 75;
	int defensiveTowerDamage = 60;
};

struct DLL_LINKAGE LeadershipSlotCapacity
{
	int leadership = 0;
	int requirement = 0;
	int maximum = 0;

	bool accepts(int count) const { return count >= 0 && count <= maximum; }
};

/// Independent identity from primary growth: an older growth hero must not adopt
/// new capacity/siege rules merely because current installed defaults enable them.
DLL_LINKAGE void validateCapabilityRules(const JsonNode & rules, bool requireAllClasses);
DLL_LINKAGE void validateResolvedCapabilityRules(const JsonNode & rules);
DLL_LINKAGE JsonNode resolveCapabilityRules(const JsonNode & rules, HeroClassID heroClass);

/// These require a nonempty resolved snapshot, not current installation defaults.
/// Skill ranks are original NONE..EXPERT; later mastery choices are separate state.
DLL_LINKAGE LeadershipCapacity capabilityLeadership(const JsonNode & resolvedRules,
	int level, int leadershipRank, uint64_t used);
/// Canonical v2 Leadership is a per-slot limit. A zero requirement means the
/// creature is outside this saved original-content table and is not restricted.
DLL_LINKAGE int capabilityLeadershipRating(const JsonNode & resolvedRules, int level);
DLL_LINKAGE int capabilityCreatureLeadershipRequirement(const JsonNode & resolvedRules, CreatureID creature);
DLL_LINKAGE std::optional<LeadershipSlotCapacity> capabilityLeadershipSlot(
	const JsonNode & resolvedRules, int level, CreatureID creature);
DLL_LINKAGE int capabilityBallistaMultiplier(const JsonNode & resolvedRules, int artilleryRank);
DLL_LINKAGE int capabilitySiegeRating(const JsonNode & resolvedRules, int warMachinesRank);
DLL_LINKAGE int capabilitySiegeOutput(const JsonNode & resolvedRules, int siegeRating, const std::string & output);
DLL_LINKAGE int capabilityDirectControlChance(const JsonNode & resolvedRules, int warMachinesRank);
}
