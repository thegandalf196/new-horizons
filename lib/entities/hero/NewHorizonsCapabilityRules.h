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
constexpr int CAPABILITY_RULESET_VERSION = 1;

struct DLL_LINKAGE SiegeCapabilities
{
	int artilleryRank;
	int ballisticsRank;
	int firstAidRank;
	int ballistaDamageMultiplier;
	int ballistaControlChance;
	int catapultControlChance;
	int firstAidControlChance;
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
DLL_LINKAGE int capabilityBallistaMultiplier(const JsonNode & resolvedRules, int artilleryRank);
}
