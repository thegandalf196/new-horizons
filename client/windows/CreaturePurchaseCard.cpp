/*
 * CreaturePurchaseCard.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CreaturePurchaseCard.h"

#include "CHeroWindow.h"
#include "QuickRecruitmentWindow.h"
#include "CCreatureWindow.h"
#include "NewHorizonsCreatureCategoryUI.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../widgets/CreatureCostBox.h"
#include "../widgets/MiscWidgets.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"

static std::optional<newHorizonsCreatures::CreatureCategoryView> currentCreatureCategory(const CCreature * creature)
{
	if(!creature || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;
	return GAME->interface()->cb->getCreatureCategory(creature->getId());
}

void CreaturePurchaseCard::initButtons()
{
	initMaxButton();
	initMinButton();
	initCreatureSwitcherButton();
}

void CreaturePurchaseCard::initMaxButton()
{
	maxButton = std::make_shared<CButton>(Point(pos.x + 52, pos.y + 180), AnimationPath::builtin("QuickRecruitmentWindow/QuickRecruitmentAllButton.def"), CButton::tooltip(), std::bind(&CSlider::scrollToMax,slider), EShortcut::RECRUITMENT_MAX);
}

void CreaturePurchaseCard::initMinButton()
{
	minButton = std::make_shared<CButton>(Point(pos.x, pos.y + 180), AnimationPath::builtin("QuickRecruitmentWindow/QuickRecruitmentNoneButton.def"), CButton::tooltip(), std::bind(&CSlider::scrollToMin,slider), EShortcut::RECRUITMENT_MIN);
}

void CreaturePurchaseCard::initCreatureSwitcherButton()
{
	creatureSwitcher = std::make_shared<CButton>(Point(pos.x + 18, pos.y-37), AnimationPath::builtin("iDv6432.def"), CButton::tooltip(), [&](){ switchCreatureLevel(); }, EShortcut::RECRUITMENT_SWITCH_LEVEL);
}

void CreaturePurchaseCard::switchCreatureLevel()
{
	OBJECT_CONSTRUCTION;
	auto index = vstd::find_pos(upgradesID, creatureOnTheCard->getId());
	auto nextCreatureId = vstd::circularAt(upgradesID, ++index);
	creatureOnTheCard = nextCreatureId.toCreature();
	if(compactLayout)
	{
		compactPortrait->setFrame(creatureOnTheCard->getIconIndex());
		compactName->setText(creatureOnTheCard->getNamePluralTranslated());
		updateCompactStats();
		removeChild(creatureClickArea.get());
		creatureClickArea = std::make_shared<CCreatureClickArea>(Point(4, 16), compactPortrait, creatureOnTheCard);
		creatureClickArea->pos.w = 58;
		creatureClickArea->pos.h = 64;
		initCompactCostInfo();
		updateCompactCostInfo(slider->getValue());
		parent->updateAllSliders();
		return;
	}

	picture = std::make_shared<CCreaturePic>(picture->pos.x - pos.x, picture->pos.y - pos.y, creatureOnTheCard);
	creatureClickArea = std::make_shared<CCreatureClickArea>(Point(picture->pos.x - pos.x, picture->pos.y - pos.y), picture, creatureOnTheCard);
	if(categoryLabel)
		categoryLabel->setText(newHorizonsCreatureCategoryUI::name(currentCreatureCategory(creatureOnTheCard),
			GAME ? &GAME->translator() : nullptr));
	parent->updateAllSliders();
	cost->set(creatureOnTheCard->getFullRecruitCost() * slider->getValue());
}

void CreaturePurchaseCard::initAmountInfo()
{
	availableAmount = std::make_shared<CLabel>(pos.x + 25, pos.y + 146, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW);
	purchaseAmount = std::make_shared<CLabel>(pos.x + 76, pos.y + 146, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);
	updateAmountInfo(0);
}

void CreaturePurchaseCard::updateAmountInfo(int value)
{
	if(compactLayout)
		availableAmount->setText(std::to_string(maxAmount - value));
	else
		availableAmount->setText(std::to_string(maxAmount-value));
	purchaseAmount->setText(std::to_string(value));
}

void CreaturePurchaseCard::initSlider()
{
	slider = std::make_shared<CSlider>(Point(pos.x, pos.y + 158), 102, std::bind(&CreaturePurchaseCard::sliderMoved, this, _1), 0, maxAmount, 0, Orientation::HORIZONTAL);
}

void CreaturePurchaseCard::initCostBox()
{
	cost = std::make_shared<CreatureCostBox>(Rect(pos.x+2, pos.y + 194, 97, 74), "");
	cost->createItems(creatureOnTheCard->getFullRecruitCost());
}

void CreaturePurchaseCard::sliderMoved(int to)
{
	updateAmountInfo(to);
	if(compactLayout)
	{
		updateCompactCostInfo(to);
		parent->updateAllSliders();
		return;
	}
	cost->set(creatureOnTheCard->getFullRecruitCost() * to);
	parent->updateAllSliders();
}

CreaturePurchaseCard::CreaturePurchaseCard(const std::vector<CreatureID> & creaturesID, Point position, int creaturesMaxAmount,
	int RecruitmentLevel, QuickRecruitmentWindow * parents, bool CompactLayout, int CardWidth, int CardHeight, bool BuiltDwelling)
	: parent(parents),
	recruitmentLevel(RecruitmentLevel),
	maxAmount(creaturesMaxAmount),
	upgradesID(creaturesID),
	compactLayout(CompactLayout),
	builtDwelling(BuiltDwelling),
	cardWidth(CardWidth),
	cardHeight(CardHeight)
{
	assert(!upgradesID.empty());
	creatureOnTheCard = builtDwelling ? upgradesID.back().toCreature() : upgradesID.front().toCreature();
	moveTo(Point(position.x, position.y));
	initView();
}

void CreaturePurchaseCard::initView()
{
	if(compactLayout)
	{
		initCompactView();
		return;
	}

	picture = std::make_shared<CCreaturePic>(pos.x, pos.y, creatureOnTheCard);
	background = std::make_shared<CPicture>(ImagePath::builtin("QuickRecruitmentWindow/CreaturePurchaseCard.png"), pos.x-4, pos.y-50);
	creatureClickArea = std::make_shared<CCreatureClickArea>(Point(pos.x, pos.y), picture, creatureOnTheCard);
	const auto category = currentCreatureCategory(creatureOnTheCard);
	const auto categoryName = newHorizonsCreatureCategoryUI::name(category, GAME ? &GAME->translator() : nullptr);
	if(!categoryName.empty())
		categoryLabel = std::make_shared<CLabel>(pos.x + 51, pos.y + 132, FONT_TINY,
			ETextAlignment::TOPCENTER, Colors::YELLOW, categoryName, 100);

	initAmountInfo();
	initSlider();
	initButtons(); // order important! buttons need slider!
	initCostBox();
}

void CreaturePurchaseCard::initCompactView()
{
	OBJECT_CONSTRUCTION;
	// A compact card shares the window's continuous leather surface. Repaint
	// that owner before changing labels so shorter values clear their old pixels.
	setRedrawParent(true);
	compactName = std::make_shared<CLabel>(cardWidth / 2, 0, FONT_TINY, ETextAlignment::TOPCENTER,
		Colors::WHITE, creatureOnTheCard->getNamePluralTranslated(), cardWidth - 8);

	if(builtDwelling && upgradesID.size() > 1)
	{
		creatureSwitcher = std::make_shared<CButton>(Point(4, 88), AnimationPath::builtin("IGPCRDIV.DEF"),
			CButton::tooltip(), [&](){ switchCreatureLevel(); }, EShortcut::RECRUITMENT_SWITCH_LEVEL);
		creatureSwitcher->setImageOrder(2, 3, 3, 3);
	}

	compactPortrait = std::make_shared<CAnimImage>(AnimationPath::builtin("TWCRPORT"), creatureOnTheCard->getIconIndex(),
		Rect(4, 16, 58, 64));
	creatureClickArea = std::make_shared<CCreatureClickArea>(Point(4, 16), compactPortrait, creatureOnTheCard);
	creatureClickArea->pos.w = 58;
	creatureClickArea->pos.h = 64;
	availableAmount = std::make_shared<CLabel>(24, 81, FONT_TINY, ETextAlignment::TOPLEFT, Colors::YELLOW,
		std::to_string(maxAmount), 38);
	compactStatHelp.push_back(std::make_shared<LRClickableAreaWText>(Rect(24, 81, 38, 11),
		LIBRARY->generaltexth->allTexts[217], LIBRARY->generaltexth->allTexts[217]));
	purchaseAmount = std::make_shared<CLabel>(62, 104, FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, "0", 38);

	const std::array<std::string, 8> statNames =
	{
		LIBRARY->generaltexth->allTexts[190], LIBRARY->generaltexth->allTexts[191], LIBRARY->generaltexth->allTexts[199],
		LIBRARY->generaltexth->allTexts[388], LIBRARY->generaltexth->allTexts[193],
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.initiative"),
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.leadershipCost"), LIBRARY->generaltexth->allTexts[194]
	};
	const std::array<std::string, 8> statDescriptions =
	{
		LIBRARY->generaltexth->translate("core.castinfo.0"), LIBRARY->generaltexth->translate("core.castinfo.1"),
		LIBRARY->generaltexth->translate("core.castinfo.2"), LIBRARY->generaltexth->translate("core.castinfo.3"),
		LIBRARY->generaltexth->translate("core.castinfo.4"),
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.initiative.description"),
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.leadershipCost.description"),
		LIBRARY->generaltexth->translate("core.castinfo.5")
	};
	const std::array<ImagePath, 8> statIcons =
	{
		ImagePath::builtin("stackWindow/iconAttack"), ImagePath::builtin("stackWindow/iconDefense"),
		ImagePath::builtin("stackWindow/iconDamage"), ImagePath::builtin("stackWindow/iconHealth"),
		ImagePath::builtin("stackWindow/iconSpeed"), ImagePath::builtin("stackWindow/iconInitiative"),
		ImagePath(), ImagePath::builtin("stackWindow/iconGrowth")
	};
	constexpr int statTop = 16;
	constexpr int statRowHeight = 11;
	constexpr int statLeft = 70;
	const int statColumnWidth = cardWidth - statLeft - 6;
	for(size_t index = 0; index < statNames.size(); ++index)
	{
		const int row = static_cast<int>(index);
		const int x = statLeft;
		const int y = statTop + row * statRowHeight;
		const int iconSize = 10;
		if(index == 6)
			compactStatIcons[index] = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_creature_leadership_20"), 0,
				Rect(x, y, iconSize, iconSize));
		else
		{
			auto icon = std::make_shared<CPicture>(statIcons[index], x, y);
			icon->scaleTo(Point(iconSize, iconSize));
			compactStatIcons[index] = icon;
		}
		const int valueX = x + statColumnWidth;
		compactStatValues[index] = std::make_shared<CLabel>(valueX, y + statRowHeight - 1,
			FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, "0", statColumnWidth - iconSize - 3);
		const std::string help = statNames[index] + ": " + statDescriptions[index];
		compactStatHelp.push_back(std::make_shared<LRClickableAreaWText>(Rect(x, y, statColumnWidth, statRowHeight),
			help, help));
	}
	updateCompactStats();

	slider = std::make_shared<CSlider>(Point(18, 106), cardWidth - 36,
		std::bind(&CreaturePurchaseCard::sliderMoved, this, _1), 0, maxAmount, 0, Orientation::HORIZONTAL);
	minButton = std::make_shared<CButton>(Point(1, 106), AnimationPath::builtin("IGPCRDIV.DEF"),
		CButton::tooltip(), std::bind(&CSlider::scrollToMin, slider), EShortcut::RECRUITMENT_MIN);
	minButton->setImageOrder(0, 1, 1, 1);
	minButton->setSoundDisabled(true);
	maxButton = std::make_shared<CButton>(Point(cardWidth - 17, 106), AnimationPath::builtin("IGPCRDIV.DEF"),
		CButton::tooltip(), std::bind(&CSlider::scrollToMax, slider), EShortcut::RECRUITMENT_MAX);
	maxButton->setImageOrder(2, 3, 3, 3);
	maxButton->setSoundDisabled(true);
	if(maxAmount <= 0)
	{
		slider->block(true);
		minButton->block(true);
		maxButton->block(true);
	}
	initCompactCostInfo();
	updateCompactCostInfo(0);
}

void CreaturePurchaseCard::updateCompactStats()
{
	if(!creatureOnTheCard)
		return;
	const std::string minDamage = std::to_string(creatureOnTheCard->getMinDamage(false));
	const std::string maxDamage = std::to_string(creatureOnTheCard->getMaxDamage(false));
	int leadershipCost = 0;
	if(GAME && GAME->interface() && GAME->interface()->cb)
	{
		const auto & capabilityRules = GAME->interface()->cb->getHeroCapabilityRules();
		if(newHorizonsHeroes::usesRules(capabilityRules) && capabilityRules["rulesetVersion"].Integer() >= 2)
			leadershipCost = newHorizonsHeroes::capabilityCreatureLeadershipRequirement(capabilityRules, creatureOnTheCard->getId());
	}
	const std::array<std::string, 8> values =
	{
		std::to_string(creatureOnTheCard->getAttack(false)),
		std::to_string(creatureOnTheCard->getDefense(false)),
		minDamage == maxDamage ? minDamage : minDamage + "-" + maxDamage,
		std::to_string(creatureOnTheCard->getMaxHealth()),
		std::to_string(creatureOnTheCard->getBaseSpeed()),
		std::to_string(creatureOnTheCard->getBaseInitiative()),
		std::to_string(leadershipCost),
		std::to_string(parent->getWeeklyGrowth(recruitmentLevel))
	};
	for(size_t index = 0; index < compactStatValues.size(); ++index)
		if(compactStatValues[index])
			compactStatValues[index]->setText(values[index]);
}

void CreaturePurchaseCard::initCompactCostInfo()
{
	for(const auto & widget : compactCostWidgets)
		removeChild(widget.get());
	compactCostWidgets.clear();
	compactCostEntries.clear();
	if(!creatureOnTheCard)
		return;

	const TResources unitCost = creatureOnTheCard->getFullRecruitCost();
	TResources::nziterator iter(unitCost);
	std::vector<GameResID> resources;
	while(iter.valid())
	{
		resources.push_back(iter->resType);
		++iter;
	}
	if(resources.empty())
		return;

	const int columns = std::min(4, static_cast<int>(resources.size()));
	constexpr int costRowHeight = 10;
	constexpr int iconSize = 9;
	const int rows = (static_cast<int>(resources.size()) + columns - 1) / columns;
	const int cellWidth = (cardWidth - 8) / columns;
	const int costTop = cardHeight - rows * costRowHeight;
	for(size_t index = 0; index < resources.size(); ++index)
	{
		const int column = static_cast<int>(index % columns);
		const int row = static_cast<int>(index / columns);
		const int x = 4 + column * cellWidth;
		const int y = costTop + row * costRowHeight;
		auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin("RESOURCE"), resources[index].getNum(),
			Rect(x, y, iconSize, iconSize));
		auto amount = std::make_shared<CLabel>(x + cellWidth, y + costRowHeight, FONT_TINY, ETextAlignment::BOTTOMRIGHT,
			Colors::WHITE, "0", cellWidth - iconSize - 1);
		auto help = std::make_shared<LRClickableAreaWText>(Rect(x, y, cellWidth, costRowHeight));
		compactCostWidgets.push_back(icon);
		compactCostWidgets.push_back(amount);
		compactCostWidgets.push_back(help);
		compactCostEntries.push_back(CompactCostEntry{resources[index], amount, help});
	}
}

void CreaturePurchaseCard::updateCompactCostInfo(int amount)
{
	if(!creatureOnTheCard)
		return;
	const TResources totalCost = creatureOnTheCard->getFullRecruitCost() * amount;
	for(const auto & entry : compactCostEntries)
	{
		const auto total = std::to_string(totalCost[entry.resource]);
		entry.amount->setText(total);
		const auto exactCost = entry.resource.toResource()->getNameTranslated() + ": " + total;
		entry.help->hoverText = exactCost;
		entry.help->text = exactCost;
	}
}

CreaturePurchaseCard::CCreatureClickArea::CCreatureClickArea(const Point & position,
	const std::shared_ptr<CIntObject> creaturePic, const CCreature * creatureOnTheCard)
	: CIntObject(SHOW_POPUP),
	creatureOnTheCard(creatureOnTheCard)
{
	(void)creaturePic;
	pos.x += position.x;
	pos.y += position.y;
	pos.w = CREATURE_WIDTH;
	pos.h = CREATURE_HEIGHT;
}

void CreaturePurchaseCard::CCreatureClickArea::showPopupWindow(const Point & cursorPosition)
{
	ENGINE->windows().createAndPushWindow<CStackWindow>(creatureOnTheCard, true);
}
