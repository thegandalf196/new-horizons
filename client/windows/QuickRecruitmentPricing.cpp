/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "QuickRecruitmentWindow.h"
#include "CreaturePurchaseCard.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../UIHelper.h"
#include "../widgets/Buttons.h"
#include "../widgets/CreatureCostBox.h"
#include "../widgets/Slider.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsEstates.h"
#include "../../lib/mapObjects/RecruitmentBasketAdmission.h"
#include "../../lib/CCreatureHandler.h"

namespace
{
int legalRecruitmentAmount(const CGTownInstance * town, CreatureID creature, int requestedMaximum)
{
	if(!town)
		return requestedMaximum;

	return UIHelper::recruitmentPosition(town->getUpperArmy(), creature, requestedMaximum).validSlot()
		? requestedMaximum : 0;
}

}

TResources QuickRecruitmentWindow::recruitmentCost(CreatureID creature, int amount,
	const CreaturePurchaseCard * card) const
{
	const auto * buyer = newHorizonsEstates::recruitmentBuyer(town->getUpperArmy());
	if(!card)
		return town->getTotalRecruitmentCost(creature, amount, buyer);
	const auto before = quoteBasket(nullptr, card).ledger;
	return town->getTotalRecruitmentCost(creature, amount, buyer, &before);
}

int QuickRecruitmentWindow::recruitmentAmount(CreatureID creature, const TResources & resources,
	int available, const newHorizonsEstates::ConscriptionLedger * ledger) const
{
	return town->getRecruitmentAmount(creature, resources, available,
		newHorizonsEstates::recruitmentBuyer(town->getUpperArmy()), ledger);
}

IGameInfoCallback * QuickRecruitmentWindow::recruitmentContext() const
{
	return town ? town->cb : nullptr;
}

QuickRecruitmentWindow::BasketQuote QuickRecruitmentWindow::quoteBasket(
	const CreaturePurchaseCard * excluded, const CreaturePurchaseCard * stopBefore) const
{
	const auto * buyer = newHorizonsEstates::recruitmentBuyer(town->getUpperArmy());
	newHorizonsEstates::RecruitmentQuoteCursor cursor(buyer);
	TResources total;
	for(const auto & card : std::views::reverse(cards))
	{
		if(card.get() == stopBefore)
			break;
		if(card.get() == excluded)
			continue;
		const auto creature = card->creatureOnTheCard->getId();
		const auto amount = card->slider->getValue();
		total += town->getTotalRecruitmentCost(creature, amount, buyer, &cursor.current());
		if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
			cursor.accept(newHorizonsEstates::quoteRecruitment(town->getRecruitmentCost(creature),
				amount, cursor.current(), true));
	}
	return {total, cursor.current()};
}

void QuickRecruitmentWindow::maxAllCards(std::vector<std::shared_ptr<CreaturePurchaseCard>> selectedCards)
{
	const auto * buyer = newHorizonsEstates::recruitmentBuyer(town->getUpperArmy());
	newHorizonsEstates::RecruitmentQuoteCursor cursor(buyer);
	auto resources = GAME->interface()->cb->getResourceAmount();
	std::vector<std::pair<std::shared_ptr<CreaturePurchaseCard>, int>> plan;
	std::vector<recruitmentBasket::Row> incoming;
	for(const auto & card : std::views::reverse(selectedCards))
	{
		const auto creature = card->creatureOnTheCard->getId();
		auto amount = legalRecruitmentAmount(town, creature,
			recruitmentAmount(creature, resources, card->maxAmount, &cursor.current()));
		if(amount > 0)
		{
			auto proposed = incoming;
			proposed.push_back({creature, amount});
			if(recruitmentBasket::fits(*town->getUpperArmy(), proposed))
				incoming = std::move(proposed);
			else
				amount = 0;
		}
		resources -= town->getTotalRecruitmentCost(creature, amount, buyer, &cursor.current());
		if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
			cursor.accept(newHorizonsEstates::quoteRecruitment(town->getRecruitmentCost(creature),
				amount, cursor.current(), true));
		plan.emplace_back(card, amount);
	}
	{
		updatingBasket = true;
		auto restore = vstd::makeScopeGuard([this] { updatingBasket = false; });
		for(const auto & [card, amount] : plan)
		{
			card->slider->setAmount(amount);
			card->slider->scrollTo(amount);
			card->sliderMoved(amount);
		}
	}
	updateAllSliders();
	maxButton->block(resources == GAME->interface()->cb->getResourceAmount());
}

void QuickRecruitmentWindow::updateAllSliders()
{
	if(updatingBasket)
		return;
	const auto resources = GAME->interface()->cb->getResourceAmount();
	for(const auto & card : cards)
	{
		// Holding other rows fixed makes this row's marginal maximum exact.
		// The cumulative discount depends on total base cost, not row order.
		const auto others = quoteBasket(card.get());
		const auto creature = card->creatureOnTheCard->getId();
		const int affordable = recruitmentAmount(creature, resources - others.cost,
			card->maxAmount, &others.ledger);
		const int maximum = std::max(0, legalRecruitmentAmount(town, creature, std::min(card->maxAmount, affordable)));
		const int current = card->slider->getValue();
		card->slider->setAmount(maximum);
		if(current > maximum)
		{
			card->slider->scrollTo(maximum);
			return; // The callback recomputes the now-clamped basket.
		}
	}
	for(const auto & card : cards)
		card->refreshCost();
	const auto total = quoteBasket().cost;
	std::vector<recruitmentBasket::Row> incoming;
	for(const auto & card : std::views::reverse(cards))
		if(card->slider->getValue() > 0)
			incoming.push_back({card->creatureOnTheCard->getId(), card->slider->getValue()});
	buyButton->block(incoming.empty() || !resources.canAfford(total)
		|| !recruitmentBasket::fits(*town->getUpperArmy(), incoming));
	if(categorizedLayout)
		updateCompactTotalCost(total);
	else
	{
		totalCost->createItems(total);
		totalCost->set(total);
	}
}
