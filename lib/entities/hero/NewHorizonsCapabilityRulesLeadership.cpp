/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsHeroRules.h"
#include "../../CCreatureHandler.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
void require(bool condition, const std::string & detail)
{
	if(!condition)
		throw std::runtime_error("Invalid New Horizons capability rules: " + detail);
}
int resolveCreature(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped creature");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), CreatureID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown creature " + key);
	return *id;
}
}

LeadershipCapacity capabilityLeadership(const JsonNode & rules, int level, int leadershipRank, uint64_t used)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	if(!usesLeadershipEfficiency(rules)) validateResolvedCapabilityRules(rules);
	if(!usesLegacyLeadership(rules))
		return {};
	require(leadershipRank >= 0 && leadershipRank <= 3, "skill rank outside NONE..EXPERT");
	require(rules["rulesetVersion"].Integer() == 1, "aggregate Leadership is legacy v1 only");
	return leadershipCapacity(rules["profile"]["base"].Integer(), rules["profile"]["perLevel"].Integer(),
		level, rules["leadership"]["skillBonusPercent"].Vector()[leadershipRank].Integer(), used,
		rules["leadership"]["minimumMovementPercent"].Integer());
}

int capabilityLeadershipRating(const JsonNode & rules, int level)
{
	if(!usesRules(rules))
		return 0;
	if(!usesLeadershipEfficiency(rules)) validateResolvedCapabilityRules(rules);
	if(!usesLegacyLeadership(rules))
		return 0;
	require(rules["rulesetVersion"].Integer() >= 2, "per-slot Leadership requires ruleset v2+");
	require(level >= 1, "hero level");
	const int64_t unscaled = static_cast<int64_t>(rules["profile"]["base"].Integer())
		+ static_cast<int64_t>(rules["profile"]["perLevel"].Integer()) * (level - 1);
	const int64_t scaled = unscaled * rules["leadership"]["globalScalePercent"].Integer() / 100;
	return static_cast<int>(std::min<int64_t>(scaled, std::numeric_limits<int>::max()));
}

int capabilityLeadershipPerLevel(const JsonNode & rules)
{
	if(!usesRules(rules))
		return 0;
	if(!usesLeadershipEfficiency(rules)) validateResolvedCapabilityRules(rules);
	if(!usesLegacyLeadership(rules))
		return 0;
	require(rules["rulesetVersion"].Integer() >= 2, "per-slot Leadership requires ruleset v2+");
	const int64_t scaled = static_cast<int64_t>(rules["profile"]["perLevel"].Integer())
		* rules["leadership"]["globalScalePercent"].Integer() / 100;
	return static_cast<int>(std::min<int64_t>(scaled, std::numeric_limits<int>::max()));
}

int capabilityCreatureLeadershipRequirement(const JsonNode & rules, CreatureID creature)
{
	if(!usesRules(rules))
		return 0;
	if(usesLeadershipEfficiency(rules) && rules.Struct().contains("profile"))
	{
		if(!creature.hasValue() || !creature.toCreature()) return 0;
		const auto & table = rules["leadership"]["resolvedCreatureRequirements"].Struct();
		const auto found = table.find(creature.toCreature()->getJsonKey());
		return found == table.end() ? 0 : static_cast<int>(found->second.Integer());
	}
	if(isWorldCapabilityRules(rules))
		validateCapabilityRules(rules, false);
	else
		validateResolvedCapabilityRules(rules);
	if(!usesLegacyLeadership(rules) || rules["rulesetVersion"].Integer() < 2 || !creature.hasValue() || !creature.toCreature())
		return 0;
	const auto & requirements = rules["leadership"]["creatureRequirements"];
	for(const auto & [key, value] : requirements.Struct())
		if(resolveCreature(key) == creature.getNum())
			return value.Integer();
	for(const auto & [key, value] : requirements.Struct())
	{
		const auto * base = CreatureID(resolveCreature(key)).toCreature();
		if(base && vstd::contains(base->upgrades, creature))
		{
			const int64_t numerator = static_cast<int64_t>(value.Integer())
				* rules["leadership"]["upgradeMultiplierPercent"].Integer();
			const int rounding = rules["leadership"]["upgradeRounding"].Integer();
			return static_cast<int>(((numerator + 50LL * rounding) / (100LL * rounding)) * rounding);
		}
	}
	return 0;
}

std::optional<LeadershipSlotCapacity> capabilityLeadershipSlot(const JsonNode & rules, int level, CreatureID creature)
{
	const int requirement = capabilityCreatureLeadershipRequirement(rules, creature);
	if(requirement <= 0)
		return std::nullopt;
	const int leadership = capabilityLeadershipRating(rules, level);
	return LeadershipSlotCapacity{leadership, requirement, leadership / requirement};
}
}
