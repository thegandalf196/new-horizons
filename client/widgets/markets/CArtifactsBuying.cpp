/*
 * CArtifactsBuying.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CArtifactsBuying.h"

#include "../../gui/Shortcut.h"
#include "../Buttons.h"
#include "../TextControls.h"

#include "../../CPlayerInterface.h"
#include "../../GameInstance.h"

#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/entities/artifact/CArtHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/IMarket.h"
#include "../../../lib/mapObjects/EstatesArtifactQuote.h"
#include "../../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../../lib/texts/CGeneralTextHandler.h"

CArtifactsBuying::CArtifactsBuying(const IMarket * market, const CGHeroInstance * hero, const std::string & title)
	: CMarketBase(market, hero)
	, CResourcesSelling([this](const std::shared_ptr<CTradeableItem> & heroSlot){CArtifactsBuying::onSlotClickPressed(heroSlot, bidTradePanel);}, GAME->interface())
{
	OBJECT_CONSTRUCTION;

	labels.emplace_back(std::make_shared<CLabel>(titlePos.x, titlePos.y, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, title));
	deal = std::make_shared<CButton>(dealButtonPos, AnimationPath::builtin("TPMRKB.DEF"),
		LIBRARY->generaltexth->zelp[595], [this](){CArtifactsBuying::makeDeal();}, EShortcut::MARKET_DEAL);
	hoarderIndicator = std::make_shared<CLabel>(299, 130, FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW, "", 560);
	labels.emplace_back(std::make_shared<CLabel>(445, 148, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[168]));

	// Player's resources
	assert(bidTradePanel);
	bidTradePanel->moveTo(pos.topLeft() + Point(39, 184));
	bidTradePanel->showcaseSlot->image->moveTo(pos.topLeft() + Point(141, 454));

	// Artifacts panel
	offerTradePanel = std::make_shared<ArtifactsPanel>([this](const std::shared_ptr<CTradeableItem> & newSlot)
		{
			CArtifactsBuying::onSlotClickPressed(newSlot, offerTradePanel);
		}, [this]()
		{
			CArtifactsBuying::updateSubtitlesForBid(EMarketMode::RESOURCE_ARTIFACT, bidTradePanel->getHighlightedItemId());
		}, market->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT));
	offerTradePanel->deleteSlotsCheck = [this](const std::shared_ptr<CTradeableItem> & slot)
	{
		return vstd::contains(this->market->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT), ArtifactID(slot->id)) ? false : true;
	};
	offerTradePanel->moveTo(pos.topLeft() + Point(328, 181));

	CMarketBase::update();
	CArtifactsBuying::deselect();
	refreshHoarderIndicator();
}

void CArtifactsBuying::deselect()
{
	CMarketBase::deselect();
	CMarketTraderText::deselect();
}

void CArtifactsBuying::makeDeal()
{
	if(ArtifactID(offerTradePanel->getHighlightedItemId()).toArtifact()->canBePutAt(hero))
	{
		GAME->interface()->cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_ARTIFACT, GameResID(bidTradePanel->getHighlightedItemId()),
			ArtifactID(offerTradePanel->getHighlightedItemId()), offerQty, hero);
		CMarketTraderText::makeDeal();
		deselect();
	}
	else
	{
		GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("core.genrltxt.326"));
	}
}

CMarketBase::MarketShowcasesParams CArtifactsBuying::getShowcasesParams() const
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
		return MarketShowcasesParams
		{
			ShowcaseParams {std::to_string(deal->isBlocked() ? 0 : bidQty), bidTradePanel->getHighlightedItemId()},
			ShowcaseParams {std::to_string(deal->isBlocked() ? 0 : offerQty), LIBRARY->artifacts()->getByIndex(offerTradePanel->getHighlightedItemId())->getIconIndex()}
		};
	else
		return MarketShowcasesParams {std::nullopt, std::nullopt};
}

void CArtifactsBuying::highlightingChanged()
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
	{
		market->getOffer(bidTradePanel->getHighlightedItemId(), offerTradePanel->getHighlightedItemId(), bidQty, offerQty, EMarketMode::RESOURCE_ARTIFACT);
		bidQty = quotedPrice(bidTradePanel->getHighlightedItemId(), bidQty);
		deal->block(GAME->interface()->cb->getResourceAmount(GameResID(bidTradePanel->getHighlightedItemId())) < bidQty || !GAME->interface()->makingTurn);
	}
	CMarketBase::highlightingChanged();
	CMarketTraderText::highlightingChanged();
}

std::string CArtifactsBuying::getTraderText()
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
	{
		MetaString message = MetaString::createFromTextID("core.genrltxt.267");
		message.replaceName(ArtifactID(offerTradePanel->getHighlightedItemId()));
		message.replaceNumber(bidQty);
		message.replaceTextID(bidQty == 1 ? "core.genrltxt.161" : "core.genrltxt.160");
		message.replaceName(GameResID(bidTradePanel->getHighlightedItemId()));
		return message.toString(&GAME->translator());
	}
	else
	{
		return madeTransaction ? LIBRARY->generaltexth->allTexts[162] : LIBRARY->generaltexth->allTexts[163];
	}
}

int CArtifactsBuying::quotedPrice(int resource, int basePrice) const
{
	ResourceSet base;
	base[GameResID(resource)] = basePrice;
	return static_cast<int>(newHorizonsEstates::artifactPurchaseQuote(base, hero).cost[GameResID(resource)]);
}

void CArtifactsBuying::updateSubtitlesForBid(EMarketMode mode, int resource)
{
	if(mode != EMarketMode::RESOURCE_ARTIFACT || resource < 0)
	{
		CMarketBase::updateSubtitlesForBid(mode, resource);
		return;
	}
	for(const auto & slot : offerTradePanel->slots)
	{
		int cost = 0;
		int quantity = 0;
		market->getOffer(resource, slot->id, cost, quantity, mode);
		offerTradePanel->updateOffer(*slot, quotedPrice(resource, cost), quantity);
	}
}

void CArtifactsBuying::refreshHoarderIndicator()
{
	std::string text;
	if(newHorizonsEstates::hasRevisedPerk(hero, "new-horizons:estates.hoarder"))
	{
		const auto calendar = hero->cb->getCalendar();
		const auto week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
		text = hero->getNewHorizonsHoarderLastWeek() == week
			? "Hoarder: this week's discount has been used."
			: "Hoarder: first artifact purchase this week costs 50% less Gold.";
	}
	if(hoarderIndicator->getText() != text)
		hoarderIndicator->setText(text);
}

void CArtifactsBuying::update()
{
	CMarketBase::update();
	refreshHoarderIndicator();
	if(bidTradePanel->isHighlighted())
		updateSubtitlesForBid(EMarketMode::RESOURCE_ARTIFACT, bidTradePanel->getHighlightedItemId());
	highlightingChanged();
}
