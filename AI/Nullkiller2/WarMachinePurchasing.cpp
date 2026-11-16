/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "AIGateway.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesArtifactQuote.h"

namespace NK2AI
{
namespace
{
	struct WarMachineShopCandidate
	{
		const CGHeroInstance * hero = nullptr;
		ArtifactID artifact = ArtifactID::NONE;
		si32 basePrice = 0;
		si32 price = 0;
		si64 valuePerGold = 0;
	};

	bool isOrdinaryWarMachine(ArtifactID artifact)
	{
		switch(artifact)
		{
		case ArtifactID::BALLISTA:
		case ArtifactID::AMMO_CART:
		case ArtifactID::FIRST_AID_TENT:
			return true;
		case ArtifactID::CATAPULT:
		default:
			return false;
		}
	}

	bool isUsefulWarMachineForHero(const CGHeroInstance & hero, ArtifactID artifact)
	{
		if(hero.Slots().empty())
			return false;

		if(artifact == ArtifactID::BALLISTA)
			return true;

		for(const auto & slot : hero.Slots())
		{
			const auto * stack = slot.second.get();
			if(artifact == ArtifactID::AMMO_CART && stack->hasBonusOfType(BonusType::SHOOTER))
				return true;
			if(artifact == ArtifactID::FIRST_AID_TENT && !stack->hasBonusOfType(BonusType::UNDEAD))
				return true;
		}

		return false;
	}

	std::optional<ArtifactPosition> findFreeWarMachineSlot(
		const CGHeroInstance & hero,
		const CArtifact & artifact,
		const std::set<ArtifactPosition> & reservedSlots)
	{
		const auto heroSlots = artifact.getPossibleSlots().find(ArtBearer::HERO);
		if(heroSlots == artifact.getPossibleSlots().end())
			return std::nullopt;

		for(const auto slot : heroSlots->second)
		{
			if(slot == ArtifactPosition::MACH4 || reservedSlots.contains(slot))
				continue;
			if(hero.getArt(slot) == nullptr && hero.isPositionFree(slot))
				return slot;
		}

		return std::nullopt;
	}

	newHorizonsEstates::ArtifactPurchaseQuote machineQuote(const CGHeroInstance & hero, int basePrice, bool reserved)
	{
		ResourceSet base;
		base[EGameResID::GOLD] = basePrice;
		return newHorizonsEstates::artifactPurchaseQuote(base, &hero, reserved);
	}
}

void AIGateway::purchaseUsefulWarMachines(
	const CGTownInstance * onlyTown,
	const CGHeroInstance * onlyHero,
	std::optional<si64> availableGold)
{
	std::vector<WarMachineShopCandidate> candidates;
	for(const auto * town : cc->getTownsInfo())
	{
		if(!town || town->tempOwner != playerID || (onlyTown && town != onlyTown))
			continue;

		const auto * hero = town->getVisitingHero();
		if(!hero || hero->tempOwner != playerID || (onlyHero && hero != onlyHero))
			continue;

		// This inventory and these prices are resolved from the town's saved
		// capability rules. Deduplicate again here so a Ballista Yard never
		// becomes a second purchase path for a Blacksmith's Ballista.
		std::set<ArtifactID> seenOffers;
		for(const auto & offer : town->getWarMachineShopOffers())
		{
			if(!seenOffers.insert(offer.artifact).second
				|| !isOrdinaryWarMachine(offer.artifact)
				|| offer.price < 0
				|| requestedWarMachineArtifacts[hero->id].contains(offer.artifact)
				|| hero->hasArt(offer.artifact, false, false)
				|| !isUsefulWarMachineForHero(*hero, offer.artifact))
				continue;

			const auto * artifact = offer.artifact.toArtifact();
			if(!artifact || !findFreeWarMachineSlot(*hero, *artifact, reservedWarMachineSlots[hero->id]))
				continue;

			const si64 artifactValue = getPotentialArtifactScore(artifact);
			if(artifactValue <= 0)
				continue;

			// Compare the normal artifact value against the price actually offered
			// by this town, so a saved-rules faction discount improves its priority.
			const auto quote = machineQuote(*hero, offer.price, reservedHoarderPurchaseHeroes.contains(hero->id));
			const auto price = quote.cost[EGameResID::GOLD];
			const si64 offeredPrice = std::max<si64>(1, price);
			const si64 valuePerGold = artifactValue * 1000 / offeredPrice;
			if(valuePerGold <= 0)
				continue;

			candidates.push_back({hero, offer.artifact, offer.price, price, valuePerGold});
		}
	}

	std::stable_sort(candidates.begin(), candidates.end(), [](const WarMachineShopCandidate & left, const WarMachineShopCandidate & right)
	{
		if(left.valuePerGold != right.valuePerGold)
			return left.valuePerGold > right.valuePerGold;
		return left.price < right.price;
	});

	const si64 replicatedGold = cc->getResourceAmount()[EGameResID::GOLD];
	const si64 spendableGold = availableGold ? std::min(replicatedGold, *availableGold) : replicatedGold;
	if(!warMachinePurchaseBudgetInitialized)
	{
		warMachinePurchaseBudgetRemaining = spendableGold;
		warMachinePurchaseBudgetInitialized = true;
	}
	else
		warMachinePurchaseBudgetRemaining = std::min(warMachinePurchaseBudgetRemaining, spendableGold);

	for(const auto & purchase : candidates)
	{
		const auto quote = machineQuote(*purchase.hero, purchase.basePrice,
			reservedHoarderPurchaseHeroes.contains(purchase.hero->id));
		const auto price = quote.cost[EGameResID::GOLD];
		if(price > warMachinePurchaseBudgetRemaining
			|| purchase.hero->hasArt(purchase.artifact, false, false)
			|| requestedWarMachineArtifacts[purchase.hero->id].contains(purchase.artifact))
			continue;

		const auto * artifact = purchase.artifact.toArtifact();
		if(!artifact || !isUsefulWarMachineForHero(*purchase.hero, purchase.artifact))
			continue;

		const auto slot = findFreeWarMachineSlot(
			*purchase.hero, *artifact, reservedWarMachineSlots[purchase.hero->id]);
		if(!slot)
			continue;

		// The callback submits the ordinary validated purchase request. Keep a
		// local reservation because the replicated resource and inventory views
		// may not include this request until its result packet arrives.
		cc->buyArtifact(purchase.hero, purchase.artifact);
		warMachinePurchaseBudgetRemaining -= price;
		if(quote.usesHoarder)
			reservedHoarderPurchaseHeroes.insert(purchase.hero->id);
		requestedWarMachineArtifacts[purchase.hero->id].insert(purchase.artifact);
		reservedWarMachineSlots[purchase.hero->id].insert(*slot);
	}
}

}
