/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGTownInstance.h"
#include "../callback/IGameInfoCallback.h"
#include "../nativeRules/CompiledCapabilityRules.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/artifact/CArtifact.h"
#include <algorithm>
#include <map>

std::vector<CGTownInstance::WarMachineShopOffer> CGTownInstance::getWarMachineShopOffers() const
{
	std::map<ArtifactID, si32> prices;
	const auto & capabilityRules = cb->getHeroCapabilityRules();
	const auto * compiled = cb->getCompiledHeroCapabilityRules();
	const bool universalBlacksmith = compiled ? compiled->version() >= 4
		: newHorizonsHeroes::usesRules(capabilityRules) && capabilityRules["rulesetVersion"].Integer() >= 4;

	auto addOffer = [&prices](ArtifactID artifact, si32 price)
	{
		if(!artifact.hasValue() || artifact == ArtifactID::CATAPULT)
			return;
		const auto * entity = artifact.toArtifact();
		if(!entity || entity->getWarMachine() == CreatureID::NONE)
			return;

		auto [entry, inserted] = prices.emplace(artifact, price);
		if(!inserted)
			entry->second = std::min(entry->second, price);
	};

	for(const auto & building : builtBuildings)
	{
		const auto artifact = getWarMachineInBuilding(building);
		if(artifact.hasValue() && artifact != ArtifactID::CATAPULT)
			if(const auto * entity = artifact.toArtifact())
				addOffer(artifact, entity->getPrice());
	}

	if(universalBlacksmith && hasBuilt(BuildingID::BLACKSMITH))
	{
		for(const auto artifact : compiled ? compiled->warMachineShopInventory()
			: newHorizonsHeroes::capabilityWarMachineShopInventory(capabilityRules))
		{
			const auto * entity = artifact.toArtifact();
			if(!entity)
				continue;
			const auto price = compiled ? compiled->warMachineShopPrice(getFactionID(), artifact, entity->getPrice())
				: newHorizonsHeroes::capabilityWarMachineShopPrice(capabilityRules, getFactionID(), artifact, entity->getPrice());
			addOffer(artifact, price.value_or(entity->getPrice()));
		}
	}

	std::vector<WarMachineShopOffer> result;
	result.reserve(prices.size());
	for(const auto artifact : {ArtifactID::BALLISTA, ArtifactID::AMMO_CART, ArtifactID::FIRST_AID_TENT})
		if(const auto offer = prices.find(artifact); offer != prices.end())
			result.push_back({offer->first, offer->second});
	for(const auto & [artifact, price] : prices)
		if(artifact != ArtifactID::BALLISTA && artifact != ArtifactID::AMMO_CART && artifact != ArtifactID::FIRST_AID_TENT)
			result.push_back({artifact, price});
	return result;
}

std::optional<int> CGTownInstance::getNewHorizonsDefensiveTowerBaseDamage() const
{
	const auto & capabilityRules = cb->getHeroCapabilityRules();
	const auto * compiled = cb->getCompiledHeroCapabilityRules();
	if(compiled)
		return compiled->hasSiege() ? std::optional(compiled->siegeOutput(0, nativeRules::SiegeOutput::DefensiveTowerDamage)) : std::nullopt;
	if(!newHorizonsHeroes::usesRules(capabilityRules) || capabilityRules["rulesetVersion"].Integer() < 3)
		return std::nullopt;

	return newHorizonsHeroes::capabilitySiegeOutput(capabilityRules, 0, "defensiveTowerDamage");
}
