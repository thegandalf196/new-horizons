/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapping/CMap.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/CGMarket.h"
#include "../lib/mapObjects/IMarket.h"
#include "../lib/mapObjects/EstatesArtifactQuote.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/entities/artifact/CArtifactInstance.h"
#include "../lib/entities/artifact/ArtifactUtils.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/ArtifactLocation.h"
#include "../lib/spells/CSpell.h"
#include "../lib/spells/NewHorizonsMagic.h"
#include "../lib/spells/NewHorizonsSpellAvailability.h"

#define COMPLAIN_RET(txt) do { complain(txt); return false; } while(0)
#define COMPLAIN_RET_FALSE_IF(cond, txt) do { if(cond) { complain(txt); return false; } } while(0)

namespace
{
ResourceSet goldPrice(TResource gold)
{
	ResourceSet result;
	result[EGameResID::GOLD] = gold;
	return result;
}
void completeHoarder(CGameHandler & handler, const CGHeroInstance * hero,
	const newHorizonsEstates::ArtifactPurchaseQuote & quote)
{
	if(quote.usesHoarder)
		handler.setObjPropertyValue(hero->id, ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK, quote.week);
}
}

bool CGameHandler::buyArtifact(ObjectInstanceID hid, ArtifactID aid)
{
	const CGHeroInstance * hero = gameInfo().getHero(hid);
	COMPLAIN_RET_FALSE_IF(nullptr == hero, "Invalid hero index");
	const CGTownInstance * town = hero->getVisitedTown();
	COMPLAIN_RET_FALSE_IF(nullptr == town, "Hero not in town");

	if (aid==ArtifactID::SPELLBOOK)
	{
		const auto quote = newHorizonsEstates::artifactPurchaseQuote(goldPrice(GameConstants::SPELLBOOK_GOLD_COST), hero);
		if ((!town->hasBuilt(BuildingID::MAGES_GUILD_1) && complain("Cannot buy a spellbook, no mage guild in the town!"))
			|| (gameInfo().getResource(hero->getOwner(), EGameResID::GOLD) < quote.cost[EGameResID::GOLD] && complain("Cannot buy a spellbook, not enough gold!"))
		    || (hero->getArt(ArtifactPosition::SPELLBOOK) && complain("Cannot buy a spellbook, hero already has a one!"))
		   )
			return false;

		giveResources(hero->getOwner(), -quote.cost);
		if(!giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK))
			throw std::runtime_error("Prevalidated spellbook purchase failed");
		completeHoarder(*this, hero, quote);
		assert(hero->getArt(ArtifactPosition::SPELLBOOK));
		giveSpells(town, hero, hero == town->getVisitingHero());
		return true;
	}
	else
	{
		const CArtifact * art = aid.toArtifact();
		COMPLAIN_RET_FALSE_IF(nullptr == art, "Invalid artifact index to buy");
		COMPLAIN_RET_FALSE_IF(art->getWarMachine() == CreatureID::NONE, "War machine artifact required");
		COMPLAIN_RET_FALSE_IF(aid == ArtifactID::CATAPULT, "Catapult cannot be purchased as an ordinary war machine!");
		COMPLAIN_RET_FALSE_IF(hero->hasArt(aid),"Hero already has this machine!");
		const auto offers = town->getWarMachineShopOffers();
		const auto offer = std::find_if(offers.begin(), offers.end(), [aid](const auto & entry)
			{ return entry.artifact == aid; });
		COMPLAIN_RET_FALSE_IF(offer == offers.end(), "This machine is unavailable here!");
		const auto quote = newHorizonsEstates::artifactPurchaseQuote(goldPrice(offer->price), hero);
		const int price = quote.cost[EGameResID::GOLD];
		COMPLAIN_RET_FALSE_IF(gameInfo().getPlayerState(hero->getOwner())->resources[EGameResID::GOLD] < price, "Not enough gold!");

		bool hasFreeSlot = false;
		for(auto slot : art->getPossibleSlots().at(ArtBearer::HERO))
			if (hero->getArt(slot) == nullptr)
				hasFreeSlot = true;

		if (!hasFreeSlot)
		{
			auto slot = art->getPossibleSlots().at(ArtBearer::HERO).front();
			removeArtifact(ArtifactLocation(hero->id, slot));
		}

		giveResource(hero->getOwner(),EGameResID::GOLD,-price);
		if(!giveHeroNewArtifact(hero, aid, ArtifactPosition::FIRST_AVAILABLE))
			throw std::runtime_error("Prevalidated war-machine purchase failed");
		completeHoarder(*this, hero, quote);
		return true;
	}
}

bool CGameHandler::buyArtifact(const IMarket *m, const CGHeroInstance *h, GameResID rid, ArtifactID aid)
{
	if(!h)
		COMPLAIN_RET("Only hero can buy artifacts!");

	if (!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT), aid))
		COMPLAIN_RET("That artifact is unavailable!");

	int b1;
	int b2;
	COMPLAIN_RET_FALSE_IF(!m->getOffer(rid, aid, b1, b2, EMarketMode::RESOURCE_ARTIFACT), "Invalid artifact offer");
	ResourceSet base;
	base[rid] = b1;
	const auto quote = newHorizonsEstates::artifactPurchaseQuote(base, h);
	b1 = quote.cost[rid];

	if (gameInfo().getResource(h->tempOwner, rid) < b1)
		COMPLAIN_RET("You can't afford to buy this artifact!");

	SetAvailableArtifacts saa;
	if(dynamic_cast<const CGTownInstance *>(m))
	{
		saa.id = ObjectInstanceID::NONE;
		saa.arts = gameState().getMap().townMerchantArtifacts;
	}
	else if(const auto *bm = dynamic_cast<const CGBlackMarket *>(m)) //black market
	{
		saa.id = bm->id;
		saa.arts = bm->artifacts;
	}
	else
		COMPLAIN_RET("Wrong marktet...");

	bool found = false;
	for (ArtifactID & art : saa.arts)
	{
		if (art == aid)
		{
			art = ArtifactID();
			found = true;
			break;
		}
	}

	if (!found)
		COMPLAIN_RET("Cannot find selected artifact on the list");
	const auto * definition = aid.toArtifact();
	COMPLAIN_RET_FALSE_IF(!definition || !definition->canBePutAt(h, ArtifactUtils::getArtAnyPosition(h, aid)),
		"The hero has no room for this artifact");

	giveResource(h->tempOwner, rid, -b1);
	sendAndApply(saa);
	if(!giveHeroNewArtifact(h, aid, ArtifactPosition::FIRST_AVAILABLE))
		throw std::runtime_error("Prevalidated merchant artifact purchase failed");
	completeHoarder(*this, h, quote);
	return true;
}

bool CGameHandler::sellArtifact(const IMarket *m, const CGHeroInstance *h, ArtifactInstanceID aid, GameResID rid)
{
	COMPLAIN_RET_FALSE_IF((!h), "Only hero can sell artifacts!");
	const CArtifactInstance *art = h->getArtByInstanceId(aid);
	COMPLAIN_RET_FALSE_IF((!art), "There is no artifact to sell!");
	COMPLAIN_RET_FALSE_IF((!art->getType()->isTradable()), "Cannot sell a war machine or spellbook!");

	int resVal = 0;
	int dump = 1;
	m->getOffer(art->getType()->getId(), rid, dump, resVal, EMarketMode::ARTIFACT_RESOURCE);

	removeArtifact(ArtifactLocation(h->id, h->getArtPos(art)));
	giveResource(h->tempOwner, rid, resVal);
	return true;
}

bool CGameHandler::buyHouseOfWisdomScroll(const IMarket *m, const CGHeroInstance *h, SpellID spell)
{
	COMPLAIN_RET_FALSE_IF(!h, "You need a hero to buy a spell scroll!");
	COMPLAIN_RET_FALSE_IF(!newHorizonsHouseOfWisdom::active(m, gameInfo().getMagicRules()), "This market does not sell spell scrolls!");

	const auto * town = dynamic_cast<const CGTownInstance *>(m);
	COMPLAIN_RET_FALSE_IF(!town, "Wrong House of Wisdom market!");
	COMPLAIN_RET_FALSE_IF(!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_SKILL), spell), "That spell scroll is unavailable!");

	const auto * definition = spell.toSpell();
	COMPLAIN_RET_FALSE_IF(!definition || !definition->isCommonHeroSpell() || definition->isAdventure(), "That spell cannot be sold as a scroll!");
	COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameInfo().getMagicRules(), spell),
		"That spell is not available for ordinary acquisition!");

	const auto quote = newHorizonsEstates::artifactPurchaseQuote(newHorizonsHouseOfWisdom::price(spell), h);
	const auto & price = quote.cost;
	const auto & resources = gameInfo().getPlayerState(h->tempOwner)->resources;
	COMPLAIN_RET_FALSE_IF(!resources.canAfford(price), "You can't afford to buy this spell scroll!");

	const auto * scroll = ArtifactID(ArtifactID::SPELL_SCROLL).toArtifact();
	const auto scrollPosition = ArtifactUtils::getArtAnyPosition(h, scroll->getId());
	COMPLAIN_RET_FALSE_IF(!scroll->canBePutAt(h, scrollPosition), "The hero has no room for this spell scroll!");

	// Remove exactly the purchased offer from the authoritative town stock.
	// The packet is applied on the server and replicated to clients, so a
	// client cannot refill or otherwise alter the storefront locally.
	auto remaining = town->getHouseOfWisdomScrolls();
	std::vector<SpellID> updatedStock(remaining.begin(), remaining.end());
	const auto it = std::find(updatedStock.begin(), updatedStock.end(), spell);
	COMPLAIN_RET_FALSE_IF(it == updatedStock.end(), "That spell scroll is unavailable!");
	updatedStock.erase(it);

	SetHouseOfWisdomScrolls stock;
	stock.townId = town->id;
	stock.scrolls = std::move(updatedStock);
	sendAndApply(stock);
	giveResources(h->tempOwner, -price);
	if(!giveHeroNewScroll(h, spell, ArtifactPosition::FIRST_AVAILABLE))
		throw std::runtime_error("Prevalidated scroll purchase failed");
	completeHoarder(*this, h, quote);
	return true;
}
