/*
 * Part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "EstatesRecruitmentQuote.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstatesPolicy.h"
#include "../CPlayerState.h"
#include "../CCreatureHandler.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/hero/NewHorizonsCastleAdventureSpecialties.h"
#include "../entities/hero/NewHorizonsNecropolisRetainedSpecialties.h"

#include <limits>
#include <algorithm>

namespace newHorizonsEstates
{
namespace
{
/// floor(value * numerator / denominator) without overflowing for numerator <= denominator.
uint64_t fractionOf(uint64_t value, uint64_t numerator, uint64_t denominator)
{
	return value / denominator * numerator + value % denominator * numerator / denominator;
}
}

RecruitmentQuote quoteRecruitment(const ResourceSet & unitPrice, int32_t quantity,
	const ConscriptionLedger & ledger, bool eligible, const RecruitmentSpecialtyDiscount & specialty)
{
	if(quantity < 0)
		throw std::runtime_error("Negative recruitment quantity");
	if(specialty.permille < 0 || specialty.permille > 1000 || specialty.combinedCapPercent < 0
		|| specialty.combinedCapPercent > 100)
		throw std::runtime_error("Invalid recruitment specialty discount");
	if(std::any_of(ledger.begin(), ledger.end(), [](int64_t value) { return value < 0; }))
		throw std::runtime_error("Negative Conscription cumulative price");
	RecruitmentQuote result;
	result.before = result.after = ledger;
	result.discounted = eligible;
	for(size_t resource = 0; resource < unitPrice.size(); ++resource)
	{
		if(unitPrice[resource] < 0)
			throw std::runtime_error("Negative recruitment price");
		const auto base = static_cast<uint64_t>(unitPrice[resource]) * static_cast<uint64_t>(quantity);
		uint64_t discount = 0;
		if(eligible && resource < ledger.size())
		{
			if(base > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - ledger[resource]))
				throw std::overflow_error("Conscription cumulative price overflow");
			result.after[resource] += static_cast<int64_t>(base);
			discount = result.after[resource] / 10 - ledger[resource] / 10;
		}
		if(specialty.permille > 0)
		{
			// The specialty adds to Conscription; together they never exceed the cap.
			const auto combined = std::min(discount + fractionOf(base, specialty.permille, 1000),
				fractionOf(base, specialty.combinedCapPercent, 100));
			discount = std::max(discount, combined);
		}
		const auto paid = base - discount;
		if(paid > std::numeric_limits<TResource>::max())
			throw std::overflow_error("Recruitment transaction price overflow");
		result.cost[resource] = static_cast<TResource>(paid);
	}
	return result;
}

int32_t affordableRecruitment(const ResourceSet & unitPrice, const ResourceSet & resources,
	int32_t available, const ConscriptionLedger & ledger, bool eligible, const RecruitmentSpecialtyDiscount & specialty)
{
	// Validate negative components even when stock is empty.
	quoteRecruitment(unitPrice, 0, ledger, eligible, specialty);
	int64_t low = 0;
	int64_t high = std::max(0, available);
	while(low < high)
	{
		const auto middle = low + (high - low + 1) / 2;
		bool affordable = false;
		try
		{
			affordable = resources.canAfford(quoteRecruitment(unitPrice, static_cast<int32_t>(middle), ledger, eligible,
				specialty).cost);
		}
		catch(const std::overflow_error &)
		{
			// An unrepresentable transaction cannot be an affordable purchase.
		}
		if(affordable)
			low = middle;
		else
			high = middle - 1;
	}
	return static_cast<int32_t>(low);
}

ConscriptionLedger recruitmentLedger(const CGHeroInstance * buyer)
{
	if(!buyer || !buyer->getOwner().isValidPlayer())
		return {};
	const auto * player = buyer->cb->getPlayerState(buyer->getOwner());
	return player ? player->estates.recruitmentBaseCost : ConscriptionLedger{};
}

bool usesConscriptionLedger(const CGHeroInstance * buyer, CreatureID creature)
{
	if(creature != CreatureID::NONE)
	{
		const auto * definition = creature.toCreature();
		if(!definition || definition->warMachine != ArtifactID::NONE)
			return false; // Machine factories deliver artifacts, not hero-army creatures.
	}
	return buyer && buyer->getOwner().isValidPlayer()
		&& hasRevisedPerk(buyer, "new-horizons:estates.conscription");
}

RecruitmentSpecialtyDiscount recruitmentSpecialtyDiscount(const CGHeroInstance * buyer, CreatureID creature)
{
	const int ancient = creature == CreatureID(CreatureID::decode("new-horizons:royalMummy"))
		? newHorizonsHeroes::ancientOathsDiscountPercent(buyer) : 0;
	const int permille = ancient > 0 ? ancient * 10 : newHorizonsHeroes::wellConnectedDiscountPermille(buyer);
	if(permille <= 0)
		return {};
	if(creature != CreatureID::NONE)
	{
		const auto * definition = creature.toCreature();
		if(!definition || definition->warMachine != ArtifactID::NONE)
			return {}; // Machine factories deliver artifacts, not hero-army creatures.
	}
	return {permille, ancient > 0 ? 100 : newHorizonsHeroes::wellConnectedCombinedCapPercent(buyer)};
}

RecruitmentQuoteCursor::RecruitmentQuoteCursor(const CGHeroInstance * buyer)
	: ledger(recruitmentLedger(buyer))
{
}

void RecruitmentQuoteCursor::accept(const RecruitmentQuote & quote)
{
	if(quote.before != ledger)
		throw std::runtime_error("Stale projected Conscription quote");
	ledger = quote.after;
}
}
