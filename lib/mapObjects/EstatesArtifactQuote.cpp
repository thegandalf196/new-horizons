/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "EstatesArtifactQuote.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstatesPolicy.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/creature/NewHorizonsMusterRules.h"

namespace newHorizonsEstates
{
void validateHoarderReceipt(int32_t lastWeek, bool supported)
{
	if(lastWeek < -1)
		throw std::runtime_error("Invalid Hoarder weekly receipt");
	if(!supported && lastWeek != -1)
		throw std::runtime_error("Hoarder weekly receipt requires revised Estates format");
}

TResource hoarderGoldCost(TResource baseGold, bool eligible, int32_t lastWeek, int32_t currentWeek)
{
	validateHoarderReceipt(lastWeek, true);
	if(baseGold < 0 || currentWeek < 0)
		throw std::runtime_error("Invalid artifact purchase quote");
	if(!eligible || lastWeek >= currentWeek)
		return baseGold;
	return baseGold / 2 + baseGold % 2;
}

ArtifactPurchaseQuote artifactPurchaseQuote(const ResourceSet & baseCost, const CGHeroInstance * hero, bool hoarderReserved)
{
	ArtifactPurchaseQuote result;
	result.cost = baseCost;
	for(const auto component : baseCost)
		if(component < 0)
			throw std::runtime_error("Negative artifact purchase price");
	if(!hero || !hero->getOwner().isValidPlayer())
		return result;
	const auto calendar = hero->cb->getCalendar();
	result.week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	const bool eligible = !hoarderReserved && hasRevisedPerk(hero, "new-horizons:estates.hoarder");
	result.usesHoarder = eligible && baseCost[EGameResID::GOLD] > 0
		&& hero->getNewHorizonsHoarderLastWeek() < result.week;
	result.cost[EGameResID::GOLD] = hoarderGoldCost(baseCost[EGameResID::GOLD], eligible,
		hero->getNewHorizonsHoarderLastWeek(), result.week);
	return result;
}
}
