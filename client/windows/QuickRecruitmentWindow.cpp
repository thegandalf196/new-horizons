/*
 * QuickRecruitmentWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "QuickRecruitmentWindow.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../CPlayerInterface.h"
#include "../widgets/Buttons.h"
#include "../widgets/CreatureCostBox.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "render/Canvas.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/CCreatureHandler.h"
#include "CreaturePurchaseCard.h"
#include "NewHorizonsCreatureCategoryUI.h"

namespace
{
std::optional<newHorizonsCreatures::CreatureCategoryView> currentCreatureCategory(const CCreature * creature)
{
	if(!creature || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;
	return GAME->interface()->cb->getCreatureCategory(creature->getId());
}

size_t categoryIndex(const newHorizonsCreatures::CreatureCategoryView & category)
{
	return static_cast<size_t>(category.category);
}
}


void QuickRecruitmentWindow::setButtons()
{
	setCancelButton();
	setBuyButton();
	setMaxButton();
}

void QuickRecruitmentWindow::setCancelButton()
{
	cancelButton = std::make_shared<CButton>(Point((pos.w / 2) + 48, 418), AnimationPath::builtin("ICN6432.DEF"), CButton::tooltip(), [&](){ close(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setBuyButton()
{
	buyButton = std::make_shared<CButton>(Point((pos.w / 2) - 32, 418), AnimationPath::builtin("IBY6432.DEF"), CButton::tooltip(), [&](){ purchaseUnits(); }, EShortcut::GLOBAL_ACCEPT);
	buyButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setMaxButton()
{
	maxButton = std::make_shared<CButton>(Point((pos.w/2)-112, 418), AnimationPath::builtin("IRCBTNS.DEF"), CButton::tooltip(), [&](){ maxAllCards(cards); }, EShortcut::RECRUITMENT_MAX);
	maxButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setCreaturePurchaseCards()
{
	categoryHeaders.fill(nullptr);
	categoryGroupRects.fill(std::nullopt);

	int availableAmount = getAvailableCreatures();
	Point position = Point((pos.w - 100*availableAmount - 8*(availableAmount-1))/2,64);
	std::vector<int> availableLevels;
	std::array<std::vector<int>, 3> categoryLevels;
	std::vector<int> uncategorizedLevels;
	for (int i = 0; i < town->getTown()->creatures.size(); i++)
	{
		if(!town->getTown()->creatures.at(i).empty() && !town->creatures.at(i).second.empty() && town->creatures[i].first)
		{
			availableLevels.push_back(i);
			if(const auto category = currentCreatureCategory(town->creatures[i].second.back().toCreature()))
				categoryLevels[categoryIndex(*category)].push_back(i);
			else
				uncategorizedLevels.push_back(i);
		}
	}

	const bool grouped = uncategorizedLevels.empty()
		&& std::ranges::any_of(categoryLevels, [](const auto & group){ return !group.empty(); });
	auto createCard = [this, &position](int level)
	{
		cards.push_back(std::make_shared<CreaturePurchaseCard>(town->creatures[level].second, position,
			town->creatures[level].first, level, this));
		position.x += 108;
	};
	if(grouped)
	{
		for(size_t index = 0; index < categoryLevels.size(); ++index)
		{
			const auto & group = categoryLevels[index];
			if(group.empty())
				continue;

			const int groupStart = position.x;
			for(const int level : group)
				createCard(level);

			const int groupWidth = static_cast<int>(group.size()) * 100 + static_cast<int>(group.size() - 1) * 8;
			const auto category = currentCreatureCategory(town->creatures[group.front()].second.back().toCreature());
			if(category)
			{
				const auto categoryName = newHorizonsCreatureCategoryUI::name(category, GAME ? &GAME->translator() : nullptr);
				if(!categoryName.empty())
				{
					categoryHeaders[index] = std::make_shared<CLabel>(groupStart + groupWidth / 2, 7, FONT_SMALL,
						ETextAlignment::TOPCENTER, Colors::YELLOW, categoryName, groupWidth + 12);
					categoryGroupRects[index] = Rect(groupStart - 6, 2, groupWidth + 12, 331);
				}
			}
		}
	}
	else
	{
		// No or incomplete saved category view means a legacy/custom game: retain
		// the original row order and construct every widget at its final position.
		for(const int level : availableLevels)
			createCard(level);
	}
	std::stable_sort(cards.begin(), cards.end(), [](const auto & lhs, const auto & rhs)
	{
		return lhs->recruitmentLevel < rhs->recruitmentLevel;
	});

	totalCost = std::make_shared<CreatureCostBox>(Rect((this->pos.w/2)-45, position.y+260, 97, 74), "");
}

void QuickRecruitmentWindow::initWindow(Rect startupPosition)
{
	pos.x = startupPosition.x + 238;
	pos.y = startupPosition.y + 45;
	pos.w = 332;
	pos.h = 461;
	int creaturesAmount = getAvailableCreatures();
	if(creaturesAmount > 3)
	{
		pos.w += 108 * (creaturesAmount - 3);
		pos.x -= 55 * (creaturesAmount - 3);
	}
	backgroundTexture = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK.pcx"), Rect(0, 0, pos.w, pos.h));
	costBackground = std::make_shared<CPicture>(ImagePath::builtin("QuickRecruitmentWindow/costBackground.png"), pos.w/2-113, 335);
}

void QuickRecruitmentWindow::maxAllCards(std::vector<std::shared_ptr<CreaturePurchaseCard> > cards)
{
	auto allAvailableResources = GAME->interface()->cb->getResourceAmount();
	for(auto i : std::views::reverse(cards))
	{
		si32 maxAmount = i->creatureOnTheCard->maxAmount(allAvailableResources);
		vstd::amin(maxAmount, i->maxAmount);

		i->slider->setAmount(maxAmount);

		if(i->slider->getValue() != maxAmount)
			i->slider->scrollTo(maxAmount);
		else
			i->sliderMoved(maxAmount);

		i->slider->scrollToMax();
		allAvailableResources -= (i->creatureOnTheCard->getFullRecruitCost() * maxAmount);
	}
	maxButton->block(allAvailableResources == GAME->interface()->cb->getResourceAmount());
}


void QuickRecruitmentWindow::purchaseUnits()
{
	int freeSlotsLeft = town->getUpperArmy()->getFreeSlots().size();

	for(auto selected : std::views::reverse(cards))
	{
		if(selected->slider->getValue() == 0)
			continue;

		const int level = selected->recruitmentLevel;

		CreatureID crid = selected->creatureOnTheCard->getId();
		SlotID dstslot = town->getUpperArmy()->getSlotFor(crid);

		if(town->getUpperArmy()->slotEmpty(dstslot))
		{
			if(freeSlotsLeft == 0)
				continue;
			freeSlotsLeft -= 1;
		}

		if(dstslot.validSlot())
			GAME->interface()->cb->recruitCreatures(town, town->getUpperArmy(), crid, selected->slider->getValue(), level);
	}
	close();
}

int QuickRecruitmentWindow::getAvailableCreatures()
{
	int creaturesAmount = 0;
	for (int i=0; i< town->getTown()->creatures.size(); i++)
		if(!town->getTown()->creatures.at(i).empty() && !town->creatures.at(i).second.empty() && town->creatures[i].first)
			creaturesAmount++;
	return creaturesAmount;
}

void QuickRecruitmentWindow::updateAllSliders()
{
	auto allAvailableResources = GAME->interface()->cb->getResourceAmount();
	for(auto i : std::views::reverse(cards))
		allAvailableResources -= (i->creatureOnTheCard->getFullRecruitCost() * i->slider->getValue());
	for(auto i : cards)
	{
		si32 maxAmount = i->creatureOnTheCard->maxAmount(allAvailableResources);
		vstd::amin(maxAmount, i->maxAmount);
		if(maxAmount < 0)
			continue;
		if(i->slider->getValue() + maxAmount < i->maxAmount)
			i->slider->setAmount(i->slider->getValue() + maxAmount);
		else
			i->slider->setAmount(i->maxAmount);
		i->slider->scrollTo(i->slider->getValue());
	}
	totalCost->createItems(GAME->interface()->cb->getResourceAmount() - allAvailableResources);
	totalCost->set(GAME->interface()->cb->getResourceAmount() - allAvailableResources);
}

QuickRecruitmentWindow::QuickRecruitmentWindow(const CGTownInstance * townd, Rect startupPosition)
	: CWindowObject(PLAYER_COLORED | BORDERED),
	town(townd)
{
	OBJECT_CONSTRUCTION;

	initWindow(startupPosition);
	setButtons();
	setCreaturePurchaseCards();
	maxAllCards(cards);

	center();
}

void QuickRecruitmentWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
	for(const auto & group : categoryGroupRects)
	{
		if(group)
			to.drawBorder(*group + pos.topLeft(), Colors::METALLIC_GOLD);
	}
}
