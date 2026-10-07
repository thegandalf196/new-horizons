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
#include "CCastleInterface.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
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
#include "NewHorizonsMusterUI.h"

#include <limits>

namespace
{
constexpr int NH_QUICK_CARD_MAX_WIDTH = 150;
constexpr int NH_QUICK_CARD_HEIGHT = 136;
constexpr int NH_QUICK_CARD_GAP = 4;
constexpr int NH_QUICK_SIDE_MARGIN = 12;
constexpr int NH_QUICK_CONTENT_TOP = 6;
constexpr int NH_QUICK_HEADING_GAP = 2;
constexpr int NH_QUICK_FOOTER_HEIGHT = 124;

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

std::vector<CreatureID> creatureVariantsAtLevel(const CGTownInstance * town, size_t level)
{
	if(!town)
		return {};
	if(level < town->creatures.size() && !town->creatures[level].second.empty())
		return town->creatures[level].second;
	if(level < town->getTown()->creatures.size())
		return town->getTown()->creatures[level];
	return {};
}

bool hasCompleteCategoryContext(const CGTownInstance * town)
{
	if(!town)
		return false;

	std::array<size_t, 3> categoryCounts{};
	bool found = false;
	for(size_t level = 0; level < town->creatures.size(); ++level)
	{
		const auto variants = creatureVariantsAtLevel(town, level);
		if(variants.empty())
			continue;

		const auto category = currentCreatureCategory(variants.front().toCreature());
		if(!category || categoryIndex(*category) >= categoryCounts.size())
			return false;
		for(const auto creatureId : variants)
		{
			const auto variantCategory = currentCreatureCategory(creatureId.toCreature());
			if(variantCategory != category)
				return false;
		}
		++categoryCounts[categoryIndex(*category)];
		found = true;
	}

	// Five cards fit at the compact 150px pitch on the supported 800px logical
	// canvas. Larger custom category bands retain the legacy usable window.
	return found && std::ranges::all_of(categoryCounts, [](size_t count){ return count <= 5; });
}

int capByLargestLeadershipSlot(const CGTownInstance * town, CreatureID creature, int requestedMaximum)
{
	if(!town)
		return requestedMaximum;

	const auto * army = town->getUpperArmy();
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(!hero)
		return requestedMaximum;

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	if(!capacity)
		return requestedMaximum;

	const auto slot = newHorizonsHeroes::recruitmentSlot(
		army, creature, std::numeric_limits<int32_t>::max());
	if(!slot.validSlot())
		return 0;

	const int64_t existingCount = army->slotEmpty(slot) ? 0 : army->getStackCount(slot);
	const int64_t headroom = std::max<int64_t>(0, static_cast<int64_t>(capacity->maximum) - existingCount);
	return static_cast<int>(std::min<int64_t>(requestedMaximum, headroom));
}

std::vector<CreatureID> displayVariantsAtLevel(const CGTownInstance * town, size_t level, bool built)
{
	auto variants = creatureVariantsAtLevel(town, level);
	if(!built && !variants.empty())
		variants.resize(1); // Show the base creature only until its dwelling is built.
	return variants;
}
}


void QuickRecruitmentWindow::setButtons()
{
	setCancelButton();
	setBuyButton();
	setMaxButton();
	setMusterButton();
}

void QuickRecruitmentWindow::setCancelButton()
{
	const int buttonY = categorizedLayout ? pos.h - 39 : 418;
	cancelButton = std::make_shared<CButton>(Point((pos.w / 2) + 48, buttonY), AnimationPath::builtin("ICN6432.DEF"),
		CButton::tooltip(), [&](){ close(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setBuyButton()
{
	const int buttonY = categorizedLayout ? pos.h - 39 : 418;
	buyButton = std::make_shared<CButton>(Point((pos.w / 2) - 32, buttonY), AnimationPath::builtin("IBY6432.DEF"),
		CButton::tooltip(), [&](){ purchaseUnits(); }, EShortcut::GLOBAL_ACCEPT);
	buyButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setMaxButton()
{
	const int buttonY = categorizedLayout ? pos.h - 39 : 418;
	maxButton = std::make_shared<CButton>(Point((pos.w / 2) - 112, buttonY), AnimationPath::builtin("IRCBTNS.DEF"),
		CButton::tooltip(), [&](){ maxAllCards(cards); }, EShortcut::RECRUITMENT_MAX);
	maxButton->setImageOrder(0, 1, 2, 3);
}

void QuickRecruitmentWindow::setMusterButton()
{
	if(!newHorizonsMusterUI::isEligible(town))
		return;

	const int buttonY = categorizedLayout ? pos.h - 39 : 418;
	const int buttonX = (pos.w / 2) + (categorizedLayout ? 124 : 92);
	musterButton = std::make_shared<CButton>(Point(buttonX, buttonY), AnimationPath::builtin("IRCBTNS.DEF"),
		CButton::tooltip("Muster", "Reinforce one town dwelling."),
		[this](){ newHorizonsMusterUI::open(town); });
	musterButton->setTextOverlay("M", FONT_SMALL, Colors::WHITE);
	if(const auto offer = newHorizonsMusterUI::offerFor(town))
	{
		musterButton->addHoverText(EButtonState::NORMAL, newHorizonsMusterUI::status(*offer));
		musterButton->block(!GAME->interface()->makingTurn || offer->usedThisWeek || newHorizonsMusterUI::targetsFor(*offer).empty());
	}
}

void QuickRecruitmentWindow::setCreaturePurchaseCards()
{
	categoryHeaders.fill(nullptr);
	categoryGroupRects.fill(std::nullopt);
	cards.clear();

	int availableAmount = getAvailableCreatures();
	Point position = Point((pos.w - 100*availableAmount - 8*(availableAmount-1))/2,64);
	std::vector<int> availableLevels;
	std::array<std::vector<int>, 3> categoryLevels;
	std::vector<int> uncategorizedLevels;
	const size_t levelCount = categorizedLayout ? town->creatures.size() : town->getTown()->creatures.size();
	for (size_t i = 0; i < levelCount; ++i)
	{
		const auto variants = creatureVariantsAtLevel(town, i);
		const bool built = i < town->creatures.size() && !town->creatures[i].second.empty();
		const bool legacyAvailable = i < town->getTown()->creatures.size() && !town->getTown()->creatures[i].empty()
			&& i < town->creatures.size() && built && town->creatures[i].first > 0;
		if(!variants.empty() && (categorizedLayout || legacyAvailable))
		{
			availableLevels.push_back(static_cast<int>(i));
			const auto category = currentCreatureCategory(variants.front().toCreature());
			if(category && categoryIndex(*category) < categoryLevels.size())
				categoryLevels[categoryIndex(*category)].push_back(static_cast<int>(i));
			else
				uncategorizedLevels.push_back(static_cast<int>(i));
		}
	}

	const bool grouped = categorizedLayout && uncategorizedLevels.empty()
		&& std::ranges::any_of(categoryLevels, [](const auto & group){ return !group.empty(); });
	auto createLegacyCard = [this, &position](int level)
	{
		cards.push_back(std::make_shared<CreaturePurchaseCard>(town->creatures[level].second, position,
			town->creatures[level].first, level, this));
		position.x += 108;
	};
	if(grouped)
	{
		constexpr int headerHeight = 12;
		const int maximumBandSize = static_cast<int>(std::ranges::max_element(categoryLevels,
			{}, [](const auto & group){ return group.size(); })->size());
		const int cardWidth = std::min(NH_QUICK_CARD_MAX_WIDTH,
			(pos.w - NH_QUICK_SIDE_MARGIN * 2 - NH_QUICK_CARD_GAP * (maximumBandSize - 1)) / maximumBandSize);
		const int cardHeight = std::min(NH_QUICK_CARD_HEIGHT,
			(pos.h - NH_QUICK_CONTENT_TOP - NH_QUICK_FOOTER_HEIGHT
				- headerHeight * static_cast<int>(categoryLevels.size())
				- NH_QUICK_HEADING_GAP * static_cast<int>(categoryLevels.size())
				- NH_QUICK_CARD_GAP * static_cast<int>(categoryLevels.size()))
			/ static_cast<int>(categoryLevels.size()));
		int bandTop = NH_QUICK_CONTENT_TOP;
		for(size_t index = 0; index < categoryLevels.size(); ++index)
		{
			const auto & group = categoryLevels[index];
			if(group.empty())
				continue;

			const int groupWidth = static_cast<int>(group.size()) * cardWidth + static_cast<int>(group.size() - 1) * NH_QUICK_CARD_GAP;
			const int groupStart = (pos.w - groupWidth) / 2;
			const auto variants = creatureVariantsAtLevel(town, static_cast<size_t>(group.front()));
			const auto category = variants.empty() ? std::nullopt : currentCreatureCategory(variants.front().toCreature());
			if(category)
			{
				const auto categoryName = newHorizonsCreatureCategoryUI::name(category, GAME ? &GAME->translator() : nullptr);
				if(!categoryName.empty())
				{
					categoryHeaders[index] = std::make_shared<CLabel>(groupStart + groupWidth / 2, bandTop, FONT_SMALL,
						ETextAlignment::TOPCENTER, Colors::YELLOW, categoryName, groupWidth + 4);
					categoryGroupRects[index] = Rect(groupStart - 3, bandTop - 1, groupWidth + 6,
						headerHeight + NH_QUICK_HEADING_GAP + cardHeight + 2);
				}
			}
			const int cardTop = bandTop + headerHeight + NH_QUICK_HEADING_GAP;
			for(size_t column = 0; column < group.size(); ++column)
			{
				const int level = group[column];
				const bool built = level < static_cast<int>(town->creatures.size()) && !town->creatures[level].second.empty();
				const auto variants = displayVariantsAtLevel(town, static_cast<size_t>(level), built);
				const int stock = built ? town->creatures[level].first : 0;
				const Point cardPosition(groupStart + static_cast<int>(column) * (cardWidth + NH_QUICK_CARD_GAP), cardTop);
				cards.push_back(std::make_shared<CreaturePurchaseCard>(variants, cardPosition, stock, level, this,
					true, cardWidth, cardHeight, built));
			}
			bandTop = cardTop + cardHeight + NH_QUICK_CARD_GAP;
		}
	}
	else
	{
		// No or incomplete saved category view means a legacy/custom game: retain
		// the original row order and construct every widget at its final position.
		for(const int level : availableLevels)
			createLegacyCard(level);
	}
	std::stable_sort(cards.begin(), cards.end(), [](const auto & lhs, const auto & rhs)
	{
		return lhs->recruitmentLevel < rhs->recruitmentLevel;
	});

	if(grouped)
		totalCost = std::make_shared<CreatureCostBox>(Rect((this->pos.w - 97) / 2, this->pos.h - NH_QUICK_FOOTER_HEIGHT + 2, 97, 74), "");
	else
		totalCost = std::make_shared<CreatureCostBox>(Rect((this->pos.w/2)-45, position.y+260, 97, 74), "");
}

void QuickRecruitmentWindow::initWindow(Rect startupPosition)
{
	categorizedLayout = hasCompleteCategoryContext(town);
	if(categorizedLayout)
	{
		const Point viewport = ENGINE->screenDimensions();
		const Point fallbackSize(800, 600);
		const Point parentSize = GAME && GAME->interface() && GAME->interface()->castleInt
			? GAME->interface()->castleInt->pos.dimensions()
			: fallbackSize;
		pos.x = 0;
		pos.y = 0;
		pos.w = std::max(1, std::min(viewport.x - 8, parentSize.x));
		pos.h = std::max(1, std::min(viewport.y - 8, parentSize.y));
		backgroundTexture = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK.pcx"), Rect(0, 0, pos.w, pos.h));
		costBackground = std::make_shared<CPicture>(ImagePath::builtin("QuickRecruitmentWindow/costBackground.png"), pos.w / 2 - 113, pos.h - NH_QUICK_FOOTER_HEIGHT);
		return;
	}
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
		maxAmount = capByLargestLeadershipSlot(town, i->creatureOnTheCard->getId(), maxAmount);

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
		SlotID dstslot = newHorizonsHeroes::recruitmentSlot(
			town->getUpperArmy(), crid, selected->slider->getValue());
		if(!dstslot.validSlot())
			continue;

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
	const size_t levelCount = categorizedLayout ? town->creatures.size() : town->getTown()->creatures.size();
	for(size_t i = 0; i < levelCount; ++i)
	{
		if(categorizedLayout)
		{
			if(!creatureVariantsAtLevel(town, i).empty())
				++creaturesAmount;
		}
		else if(i < town->creatures.size() && !town->getTown()->creatures[i].empty()
			&& !town->creatures[i].second.empty() && town->creatures[i].first)
			++creaturesAmount;
	}
	return creaturesAmount;
}

int QuickRecruitmentWindow::getWeeklyGrowth(int recruitmentLevel) const
{
	if(!town || recruitmentLevel < 0 || static_cast<size_t>(recruitmentLevel) >= town->creatures.size()
		|| town->creatures[recruitmentLevel].second.empty())
		return 0;
	return town->getGrowthInfo(recruitmentLevel).totalGrowth();
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
		const int maximumByLeadership = capByLargestLeadershipSlot(
			town, i->creatureOnTheCard->getId(), i->maxAmount);
		const int currentAmount = i->slider->getValue();
		if(maxAmount < 0)
			continue;

		const int64_t affordableTotal = static_cast<int64_t>(currentAmount) + maxAmount;
		const int maximumTotal = static_cast<int>(std::max<int64_t>(0,
			std::min<int64_t>({i->maxAmount, maximumByLeadership, affordableTotal})));
		i->slider->setAmount(maximumTotal);
		if(currentAmount > maximumTotal)
		{
			// Re-enter with the clamped selection so resource availability for the
			// remaining cards is recomputed from the corrected purchase total.
			i->slider->scrollTo(maximumTotal);
			return;
		}
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
