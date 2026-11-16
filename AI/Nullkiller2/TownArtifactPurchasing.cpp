/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "AIGateway.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesArtifactQuote.h"

namespace NK2AI
{
void AIGateway::purchaseTownArtifacts(const CGTownInstance * town, HeroPtr heroPtr, ResourceSet & availableResources)
{
	const auto * hero = heroPtr.get();
	auto quoteFor = [&](const ResourceSet & base)
	{
		return newHorizonsEstates::artifactPurchaseQuote(base, hero,
			reservedHoarderPurchaseHeroes.contains(hero->id));
	};
	auto reserve = [&](const newHorizonsEstates::ArtifactPurchaseQuote & quote)
	{
		availableResources -= quote.cost;
		if(quote.usesHoarder)
			reservedHoarderPurchaseHeroes.insert(hero->id);
	};

	ResourceSet bookBase;
	bookBase[EGameResID::GOLD] = GameConstants::SPELLBOOK_GOLD_COST;
	const auto book = quoteFor(bookBase);
	if(nullkiller->heroManager->getHeroRoleOrDefault(heroPtr) == HeroRole::MAIN
		&& !hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1)
		&& nullkiller->getFreeGold() >= book.cost[EGameResID::GOLD]
		&& availableResources.canAfford(book.cost))
	{
		cc->buyArtifact(hero, ArtifactID::SPELLBOOK);
		reserve(book);
	}

	if(!newHorizonsHouseOfWisdom::active(town, cc->getMagicRules()))
		return;
	for(const auto & offer : town->availableItemsIds(EMarketMode::RESOURCE_SKILL))
	{
		const auto spell = offer.as<SpellID>();
		if(!spell.hasValue())
			continue;
		const auto scroll = quoteFor(newHorizonsHouseOfWisdom::price(spell));
		if(!availableResources.canAfford(scroll.cost))
			continue;
		if(ArtifactUtils::getArtAnyPosition(hero, ArtifactID::SPELL_SCROLL) == ArtifactPosition::PRE_FIRST)
			break;

		// Reserve submitted costs and the weekly allowance until replicas catch up.
		// Only the authoritative purchase records a successful Hoarder receipt.
		cc->trade(town->getObjInstanceID(), EMarketMode::RESOURCE_SKILL,
			GameResID(GameResID::GOLD), spell, 1, hero);
		reserve(scroll);
		break;
	}
}
}
