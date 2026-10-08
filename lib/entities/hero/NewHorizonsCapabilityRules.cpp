/*
 * NewHorizonsCapabilityRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsHeroRules.h"
#include "CHeroClass.h"
#include "CHeroClassHandler.h"
#include "../faction/CFaction.h"
#include "../../GameLibrary.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include "../../CCreatureHandler.h"

#include <cmath>
#include <set>
#include <stdexcept>

const JsonNode & IGameInfoCallback::getHeroCapabilityRules() const
{
	static const JsonNode legacy;
	return legacy;
}

namespace newHorizonsHeroes
{
namespace
{
void require(bool condition, const std::string & detail)
{
	if(!condition)
		throw std::runtime_error("Invalid New Horizons capability rules: " + detail);
}

bool integer(const JsonNode & value, int minimum, int maximum)
{
	return value.isNumber() && std::isfinite(value.Float())
		&& value.Float() >= minimum && value.Float() <= maximum
		&& std::floor(value.Float()) == value.Float();
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

int resolveClass(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped class");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), HeroClassID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown class " + key);
	return *id;
}

int resolveCreature(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped creature");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), CreatureID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown creature " + key);
	return *id;
}

ArtifactID resolveArtifact(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped artifact");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), ArtifactID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown artifact " + key);
	return ArtifactID(*id);
}

int resolveFaction(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped faction");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), FactionID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown faction " + key);
	return *id;
}

void rankValues(const JsonNode & node, int minimum, int maximum, int untrained)
{
	require(node.isVector() && node.Vector().size() == 4, "four skill rank values required");
	for(const auto & value : node.Vector())
		require(integer(value, minimum, maximum), "skill rank value");
	require(node.Vector().front().Integer() == untrained, "untrained skill value");
}

const std::array<ArtifactID, 3> & ordinaryWarMachineInventory()
{
	static const std::array<ArtifactID, 3> result = {
		ArtifactID::BALLISTA,
		ArtifactID::AMMO_CART,
		ArtifactID::FIRST_AID_TENT
	};
	return result;
}

void validateWarMachineShop(const JsonNode & node)
{
	fields(node, {"inventory", "discountPercent", "roundingGold", "favoredMachineByFaction"});
	require(node["inventory"].isVector() && node["inventory"].Vector().size() == ordinaryWarMachineInventory().size(),
		"ordinary war machine inventory");
	for(size_t index = 0; index < ordinaryWarMachineInventory().size(); ++index)
		require(resolveArtifact(node["inventory"].Vector()[index].String()) == ordinaryWarMachineInventory()[index],
			"ordinary war machine inventory must be Ballista, Ammo Cart, First Aid Tent in order");
	require(integer(node["discountPercent"], 1, 100), "favored machine discountPercent");
	require(integer(node["roundingGold"], 1, 100000), "favored machine roundingGold");

	const auto & favored = node["favoredMachineByFaction"];
	require(favored.isStruct() && favored.Struct().size() == 9, "favored machine mapping must cover all nine towns");
	std::set<int> seenFactions;
	for(const auto & [factionKey, artifactValue] : favored.Struct())
	{
		require(seenFactions.insert(resolveFaction(factionKey)).second, "duplicate resolved faction in favored machine mapping");
		const auto artifact = resolveArtifact(artifactValue.String());
		require(std::find(ordinaryWarMachineInventory().begin(), ordinaryWarMachineInventory().end(), artifact)
			!= ordinaryWarMachineInventory().end(), "favored artifact is outside ordinary inventory");
	}
}

void common(const JsonNode & rules)
{
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], 1, CAPABILITY_RULESET_VERSION), "rulesetVersion");
	if(!rules["skeletonTransformer"].isNull())
	{
		require(rules["rulesetVersion"].Integer() >= 4, "Skeleton Transformer requires ruleset v4");
		fields(rules["skeletonTransformer"], {"aggregateHealthPercent"});
		require(integer(rules["skeletonTransformer"]["aggregateHealthPercent"], 1, 100), "Skeleton Transformer health percent");
	}
	if(rules["rulesetVersion"].Integer() == 1)
	{
		fields(rules["leadership"], {"skillBonusPercent", "minimumMovementPercent"});
		rankValues(rules["leadership"]["skillBonusPercent"], 0, 1000, 0);
		require(integer(rules["leadership"]["minimumMovementPercent"], 1, 100), "minimumMovementPercent");
	}
	else
	{
		fields(rules["leadership"], {"globalScalePercent", "upgradeMultiplierPercent", "upgradeRounding", "creatureRequirements"});
		require(integer(rules["leadership"]["globalScalePercent"], 1, 10000), "globalScalePercent");
		require(integer(rules["leadership"]["upgradeMultiplierPercent"], 1, 10000), "upgradeMultiplierPercent");
		require(integer(rules["leadership"]["upgradeRounding"], 1, 1000), "upgradeRounding");
		require(rules["leadership"]["creatureRequirements"].isStruct()
			&& !rules["leadership"]["creatureRequirements"].Struct().empty(), "creatureRequirements");
		std::set<int> creatures;
		for(const auto & [key, value] : rules["leadership"]["creatureRequirements"].Struct())
		{
			require(creatures.insert(resolveCreature(key)).second, "duplicate resolved creature");
			require(integer(value, 1, 1000000), "creature Leadership Requirement");
		}
	}
	if(rules["rulesetVersion"].Integer() < 3)
	{
		fields(rules["siege"], {"ballistaDamageMultiplier"});
		rankValues(rules["siege"]["ballistaDamageMultiplier"], 1, 100, 1);
	}
	else
	{
		fields(rules["siege"], {"ballistaDamageMultiplier", "siegeRating", "outputs", "directControlChance"});
		rankValues(rules["siege"]["ballistaDamageMultiplier"], 1, 100, 1);
		rankValues(rules["siege"]["siegeRating"], 0, 10000, 0);
		fields(rules["siege"]["outputs"], {"ballistaDamage", "catapultStructuralDamage", "firstAidHealing", "defensiveTowerDamage"});
		for(const auto * output : {"ballistaDamage", "catapultStructuralDamage", "firstAidHealing", "defensiveTowerDamage"})
		{
			fields(rules["siege"]["outputs"][output], {"base", "siegeCoefficientHalf"});
			require(integer(rules["siege"]["outputs"][output]["base"], 0, 1000000), std::string(output) + " base");
			require(integer(rules["siege"]["outputs"][output]["siegeCoefficientHalf"], 0, 1000000), std::string(output) + " coefficient");
		}
		rankValues(rules["siege"]["directControlChance"], 0, 100, 0);
	}
	if(rules["rulesetVersion"].Integer() < 4)
		require(rules["warMachineShop"].isNull(), "legacy rules cannot contain warMachineShop");
	else
		validateWarMachineShop(rules["warMachineShop"]);
}

void profile(const JsonNode & node)
{
	fields(node, {"base", "perLevel"});
	require(integer(node["base"], 1, 1000000), "class leadership base");
	require(integer(node["perLevel"], 0, 1000000), "class leadership perLevel");
}

void resolvedRank(const JsonNode & rules, int rank)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	validateResolvedCapabilityRules(rules);
	require(rank >= 0 && rank <= 3, "skill rank outside NONE..EXPERT");
}
}

void validateCapabilityRules(const JsonNode & rules, bool requireAllClasses)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "classProfiles", "leadership", "siege", "warMachineShop", "skeletonTransformer"});
	common(rules);
	require(rules["classProfiles"].isStruct() && !rules["classProfiles"].Struct().empty(), "class profiles");
	std::set<int> seen;
	for(const auto & [key, value] : rules["classProfiles"].Struct())
	{
		require(seen.insert(resolveClass(key)).second, "duplicate resolved class");
		profile(value);
	}
	if(requireAllClasses)
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(seen.count(heroClass->getIndex()), "missing class " + heroClass->getJsonKey());
}

void validateResolvedCapabilityRules(const JsonNode & rules)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "profile", "leadership", "siege", "warMachineShop", "skeletonTransformer"});
	common(rules);
	profile(rules["profile"]);
}

JsonNode resolveCapabilityRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	validateCapabilityRules(rules, false);
	JsonNode result;
	for(const auto * key : {"schemaVersion", "rulesetVersion", "leadership", "siege"})
		result[key] = rules[key];
	if(rules["rulesetVersion"].Integer() >= 4)
		result["warMachineShop"] = rules["warMachineShop"];
	if(!rules["skeletonTransformer"].isNull())
		result["skeletonTransformer"] = rules["skeletonTransformer"];
	for(const auto & [key, value] : rules["classProfiles"].Struct())
		if(resolveClass(key) == heroClass.getNum())
			result["profile"] = value;
	validateResolvedCapabilityRules(result);
	return result;
}

std::optional<int> capabilitySkeletonTransformerHealthPercent(const JsonNode & rules)
{
	if(!usesRules(rules) || rules["skeletonTransformer"].isNull())
		return std::nullopt;
	if(rules.Struct().contains("classProfiles"))
		validateCapabilityRules(rules, false);
	else
		validateResolvedCapabilityRules(rules);
	return static_cast<int>(rules["skeletonTransformer"]["aggregateHealthPercent"].Integer());
}

LeadershipCapacity capabilityLeadership(const JsonNode & rules, int level, int leadershipRank, uint64_t used)
{
	resolvedRank(rules, leadershipRank);
	require(rules["rulesetVersion"].Integer() == 1, "aggregate Leadership is legacy v1 only");
	return leadershipCapacity(rules["profile"]["base"].Integer(), rules["profile"]["perLevel"].Integer(),
		level, rules["leadership"]["skillBonusPercent"].Vector()[leadershipRank].Integer(), used,
		rules["leadership"]["minimumMovementPercent"].Integer());
}

int capabilityLeadershipRating(const JsonNode & rules, int level)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	validateResolvedCapabilityRules(rules);
	require(rules["rulesetVersion"].Integer() >= 2, "per-slot Leadership requires ruleset v2+");
	require(level >= 1, "hero level");
	const int64_t unscaled = static_cast<int64_t>(rules["profile"]["base"].Integer())
		+ static_cast<int64_t>(rules["profile"]["perLevel"].Integer()) * (level - 1);
	const int64_t scaled = unscaled * rules["leadership"]["globalScalePercent"].Integer() / 100;
	return static_cast<int>(std::min<int64_t>(scaled, std::numeric_limits<int>::max()));
}

int capabilityLeadershipPerLevel(const JsonNode & rules)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	validateResolvedCapabilityRules(rules);
	require(rules["rulesetVersion"].Integer() >= 2, "per-slot Leadership requires ruleset v2+");
	const int64_t scaled = static_cast<int64_t>(rules["profile"]["perLevel"].Integer())
		* rules["leadership"]["globalScalePercent"].Integer() / 100;
	return static_cast<int>(std::min<int64_t>(scaled, std::numeric_limits<int>::max()));
}

int capabilityCreatureLeadershipRequirement(const JsonNode & rules, CreatureID creature)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	if(rules.Struct().contains("classProfiles"))
		validateCapabilityRules(rules, false);
	else
		validateResolvedCapabilityRules(rules);
	if(rules["rulesetVersion"].Integer() < 2 || !creature.hasValue() || !creature.toCreature())
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

std::optional<LeadershipSlotCapacity> capabilityLeadershipSlot(
	const JsonNode & rules, int level, CreatureID creature)
{
	const int requirement = capabilityCreatureLeadershipRequirement(rules, creature);
	if(requirement <= 0)
		return std::nullopt;
	const int leadership = capabilityLeadershipRating(rules, level);
	return LeadershipSlotCapacity{leadership, requirement, leadership / requirement};
}

int capabilityBallistaMultiplier(const JsonNode & rules, int artilleryRank)
{
	resolvedRank(rules, artilleryRank);
	return rules["siege"]["ballistaDamageMultiplier"].Vector()[artilleryRank].Integer();
}

namespace
{
int v3SiegeValue(const JsonNode & rules, int rank, const char * key)
{
	resolvedRank(rules, rank);
	require(rules["rulesetVersion"].Integer() >= 3, std::string(key) + " requires capability ruleset v3");
	return rules["siege"][key].Vector()[rank].Integer();
}
}

int capabilitySiegeRating(const JsonNode & rules, int warMachinesRank)
{
	return v3SiegeValue(rules, warMachinesRank, "siegeRating");
}

int capabilitySiegeOutput(const JsonNode & rules, int siegeRating, const std::string & output)
{
	require(usesRules(rules) && rules["rulesetVersion"].Integer() >= 3, "Siege output requires capability ruleset v3");
	require(siegeRating >= 0, "Siege rating must be nonnegative");
	const auto & formula = rules["siege"]["outputs"][output];
	require(!formula.isNull(), "unknown Siege output " + output);
	return formula["base"].Integer() + formula["siegeCoefficientHalf"].Integer() * siegeRating / 2;
}

int capabilityDirectControlChance(const JsonNode & rules, int warMachinesRank)
{
	return v3SiegeValue(rules, warMachinesRank, "directControlChance");
}

std::vector<ArtifactID> capabilityWarMachineShopInventory(const JsonNode & rules)
{
	if(!usesRules(rules))
		return {};
	validateCapabilityRules(rules, false);
	if(rules["rulesetVersion"].Integer() < 4)
		return {};

	std::vector<ArtifactID> result;
	for(const auto & artifact : rules["warMachineShop"]["inventory"].Vector())
		result.push_back(resolveArtifact(artifact.String()));
	return result;
}

std::optional<int> capabilityWarMachineShopPrice(
	const JsonNode & rules, FactionID faction, ArtifactID artifact, int ordinaryPrice)
{
	const auto inventory = capabilityWarMachineShopInventory(rules);
	if(inventory.empty() || !vstd::contains(inventory, artifact))
		return std::nullopt;
	require(faction.hasValue() && faction.toFaction(), "valid town faction required for shop price");
	require(ordinaryPrice >= 0, "ordinary war machine price must be nonnegative");

	const auto & shop = rules["warMachineShop"];
	const auto favored = shop["favoredMachineByFaction"][faction.toFaction()->getJsonKey()];
	if(favored.isNull() || resolveArtifact(favored.String()) != artifact)
		return ordinaryPrice;

	const int64_t rounding = shop["roundingGold"].Integer();
	const int64_t percent = shop["discountPercent"].Integer();
	const int64_t denominator = 100 * rounding;
	const int64_t discounted = static_cast<int64_t>(ordinaryPrice) * percent;
	const int64_t rounded = ((discounted + denominator / 2) / denominator) * rounding;
	require(rounded <= std::numeric_limits<int>::max(), "favored machine price overflow");
	return static_cast<int>(rounded);
}
}
