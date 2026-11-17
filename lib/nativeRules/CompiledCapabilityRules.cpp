/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledCapabilityRules.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nativeRules
{
namespace
{
size_t checkedRank(int rank)
{
	if(rank < 0 || rank > 3)
		throw std::runtime_error("Capability rank outside NONE..EXPERT");
	return static_cast<size_t>(rank);
}
}

CompiledCapabilityRules::CompiledCapabilityRules(const JsonNode & resolved, CapabilityContext context)
{
	if(context == CapabilityContext::World) newHorizonsHeroes::validateCapabilityRules(resolved, false);
	else newHorizonsHeroes::validateResolvedCapabilityRules(resolved);
	if(resolved.isNull() || (resolved.isStruct() && resolved.Struct().empty()))
		return;
	policy = static_cast<int>(resolved["rulesetVersion"].Integer());
	leadershipRules.emplace(resolved);
	const auto identifier = [](const std::string & type, const std::string & key)
	{
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), type, key, true);
		if(!id) throw std::runtime_error("Missing admitted capability identifier " + key);
		return *id;
	};
	if(resolved.Struct().contains("artifactManaRegeneration"))
		for(const auto & [key, tier] : resolved["artifactManaRegeneration"].Struct())
			manaTiers.emplace(ArtifactID(identifier(ArtifactID::entityType(), key)),
				ManaTier{static_cast<int>(tier["minimum"].Integer()), static_cast<int>(tier["percent"].Integer())});
	if(resolved.Struct().contains("glyphsOfFearAura"))
		fearAura = newHorizonsHeroes::GlyphsOfFearAura{static_cast<int>(resolved["glyphsOfFearAura"]["radius"].Integer()),
			static_cast<int>(resolved["glyphsOfFearAura"]["morale"].Integer())};
	if(!resolved["skeletonTransformer"].isNull())
		skeletonHealthPercent = static_cast<int>(resolved["skeletonTransformer"]["aggregateHealthPercent"].Integer());
	if(policy >= 4)
	{
		const auto & shop = resolved["warMachineShop"];
		for(const auto & value : shop["inventory"].Vector())
			shopInventory.emplace_back(identifier(ArtifactID::entityType(), value.String()));
		for(const auto & [faction, artifact] : shop["favoredMachineByFaction"].Struct())
			favoredMachines.emplace(FactionID(identifier(FactionID::entityType(), faction)),
				ArtifactID(identifier(ArtifactID::entityType(), artifact.String())));
		shopDiscountPercent = static_cast<int>(shop["discountPercent"].Integer());
		shopRoundingGold = static_cast<int>(shop["roundingGold"].Integer());
	}
	for(size_t rank = 0; rank < ballistaMultiplier.size(); ++rank)
		ballistaMultiplier[rank] = static_cast<int>(resolved["siege"]["ballistaDamageMultiplier"][rank].Integer());
	if(!hasSiege()) return;
	const int canonicalId = SecondarySkill::decode("new-horizons:warMachines");
	if(canonicalId >= 0 && SecondarySkill::encode(canonicalId) == "new-horizons:warMachines")
		machineSkill = SecondarySkill(canonicalId);
	for(size_t rank = 0; rank < siegeRating.size(); ++rank)
	{
		siegeRating[rank] = static_cast<int>(resolved["siege"]["siegeRating"][rank].Integer());
		controlChance[rank] = static_cast<int>(resolved["siege"]["directControlChance"][rank].Integer());
	}
	constexpr std::array keys{"ballistaDamage", "catapultStructuralDamage", "firstAidHealing", "defensiveTowerDamage"};
	for(size_t i = 0; i < keys.size(); ++i)
	{
		const auto & formula = resolved["siege"]["outputs"][keys[i]];
		output[i] = {static_cast<int>(formula["base"].Integer()), static_cast<int>(formula["siegeCoefficientHalf"].Integer())};
	}
}

std::optional<int> CompiledCapabilityRules::artifactManaRegeneration(ArtifactID artifact, int maximumMana) const
{
	const auto tier = manaTiers.find(artifact);
	if(tier == manaTiers.end()) return std::nullopt;
	const int64_t percentage = static_cast<int64_t>(std::max(0, maximumMana)) * tier->second.percent / 100;
	return static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(),
		std::max<int64_t>(tier->second.minimum, percentage)));
}

std::optional<int> CompiledCapabilityRules::warMachineShopPrice(FactionID faction, ArtifactID artifact, int ordinaryPrice) const
{
	if(std::find(shopInventory.begin(), shopInventory.end(), artifact) == shopInventory.end()) return std::nullopt;
	if(!faction.hasValue() || !faction.toFaction() || ordinaryPrice < 0)
		throw std::runtime_error("Invalid war-machine shop price context");
	const auto favored = favoredMachines.find(faction);
	if(favored == favoredMachines.end() || favored->second != artifact) return ordinaryPrice;
	const int64_t denominator = 100LL * shopRoundingGold;
	const int64_t discounted = static_cast<int64_t>(ordinaryPrice) * shopDiscountPercent;
	const int64_t rounded = ((discounted + denominator / 2) / denominator) * shopRoundingGold;
	if(rounded > std::numeric_limits<int>::max()) throw std::runtime_error("Favored machine price overflow");
	return static_cast<int>(rounded);
}

int CompiledCapabilityRules::ballistaDamageMultiplier(int rank) const
{
	if(policy == 0) throw std::runtime_error("Missing captured capability rules");
	return ballistaMultiplier.at(checkedRank(rank));
}

int CompiledCapabilityRules::rating(int rank) const
{
	if(!hasSiege()) throw std::runtime_error("Siege requires capability ruleset v3");
	return siegeRating.at(checkedRank(rank));
}

int CompiledCapabilityRules::directControlChance(int rank) const
{
	if(!hasSiege()) throw std::runtime_error("Siege requires capability ruleset v3");
	return controlChance.at(checkedRank(rank));
}

int CompiledCapabilityRules::siegeOutput(int rating, SiegeOutput kind) const
{
	if(!hasSiege() || rating < 0) throw std::runtime_error("Invalid Siege output context");
	const auto & formula = output.at(static_cast<size_t>(kind));
	const int64_t result = formula.base + static_cast<int64_t>(formula.coefficientHalf) * rating / 2;
	if(result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
		throw std::runtime_error("Siege output overflow");
	return static_cast<int>(result);
}
}
