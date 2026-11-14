/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCastleInterface.h"
#include "FortRecruitmentPresentation.h"
#include "FortCompactCreatureLayout.h"
#include "TownGrowthPresentation.h"
#include "NewHorizonsCreatureCategoryUI.h"
#include "CCreatureWindow.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../gui/WindowHandler.h"
#include "render/Canvas.h"
#include "render/Colors.h"
#include "render/IFont.h"
#include "render/IRenderHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"

static std::optional<newHorizonsCreatures::CreatureCategoryView> currentCreatureCategory(const CCreature * creature)
{
	if(!creature || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;
	return GAME->interface()->cb->getCreatureCategory(creature->getId());
}

using namespace fortRecruitmentPresentation;

namespace
{
std::array<std::string, NH_FORT_STAT_COUNT> statNames()
{
	return
	{
		LIBRARY->generaltexth->allTexts[190],
		LIBRARY->generaltexth->allTexts[191],
		LIBRARY->generaltexth->allTexts[199],
		LIBRARY->generaltexth->allTexts[388],
		LIBRARY->generaltexth->allTexts[193],
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.initiative"),
		LIBRARY->generaltexth->allTexts[194]
	};
}

std::array<std::string, NH_FORT_STAT_COUNT> statValues(const CCreature * creature, const CGTownInstance * town, int level)
{
	const auto number = [](int value) { return value ? std::to_string(value) : std::string(); };
	std::string damage = number(creature->getMinDamage(false));
	if(creature->getMinDamage(false) && creature->getMaxDamage(false)
		&& creature->getMaxDamage(false) != creature->getMinDamage(false))
		damage += "-" + std::to_string(creature->getMaxDamage(false));
	return {number(creature->getAttack(false)), number(creature->getDefense(false)), damage,
		number(creature->getMaxHealth()), number(creature->getBaseSpeed()), number(creature->getBaseInitiative()),
		town->creatures[level].second.empty() ? "0" : number(town->creatureGrowth(level))};
}

CompactCreatureLayout creatureCardLayout(const CCreature * creature, const CGTownInstance * town, int level, int width, int height)
{
	const auto font = ENGINE->renderHandler().loadFont(FONT_TINY);
	const auto names = statNames();
	const auto values = statValues(creature, town, level);
	int namedWidth = 0;
	int symbolWidth = 0;
	for(size_t index = 0; index < names.size(); ++index)
	{
		const int valueWidth = static_cast<int>(font->getStringWidth(values[index]));
		const int nameWidth = static_cast<int>(font->getStringWidth(names[index]));
		// Original 19px stat-symbol column, 2px gap, and LabeledValue's 6px text inset.
		namedWidth = std::max(namedWidth, 19 + 2 + nameWidth + valueWidth + 8);
		symbolWidth = std::max(symbolWidth, 19 + 2 + valueWidth + 8);
	}
	return compactCreatureLayout(width, height, static_cast<int>(font->getLineHeight()), namedWidth, symbolWidth);
}
}

CFortScreen::RecruitArea::RecruitArea(int posX, int posY, const CGTownInstance * Town, int Level, int Width, int Height, bool Ranked, bool CompactStats):
	town(Town),
	level(Level),
	availableCount(nullptr),
	cardWidth(Width),
	cardHeight(Height),
	rankedLayout(Ranked),
	compactStatGrid(CompactStats)
{
	OBJECT_CONSTRUCTION;
	pos.x +=posX;
	pos.y +=posY;
	pos.w = cardWidth;
	pos.h = cardHeight;
	if(!town->creatures[level].second.empty())
		addUsedEvents(LCLICK | HOVER);
	addUsedEvents(SHOW_POPUP);
	if(rankedLayout)
		cardBackground = createResponsiveFortCardBackground(Point(cardWidth, cardHeight));
	else
		icons = std::make_shared<CPicture>(ImagePath::builtin("TPCAINFO"), 261, 3);
	createBuildingPresentation();
	if(getMyCreature())
		createCreaturePresentation();
}

void CFortScreen::RecruitArea::createBuildingPresentation()
{
	const int compactFontHeight = rankedLayout ? rankedFortTinyFontHeight() : 0;
	const int compactTitleCenterY = rankedLayout ? 1 + compactFontHeight / 2 : 0;
	if(getMyBuilding() != nullptr)
	{
		if(!rankedLayout)
		{
			buildingIcon = std::make_shared<CAnimImage>(town->getTown()->clientInfo.buildingsIcons, getMyBuilding()->bid, 0, 4, 21);
			buildingName = std::make_shared<CLabel>(78, 101, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, getMyBuilding()->getNameTranslated(), 152);
		}

		if(town->hasBuilt(getMyBuilding()->bid))
		{
			ui32 available = town->creatures[level].first;
			if(rankedLayout)
				availableCount = std::make_shared<CLabel>(18,
					compactStatGrid ? compactTitleCenterY : cardHeight - compactFontHeight / 2 - 1, FONT_TINY, ETextAlignment::CENTER,
					Colors::WHITE, std::to_string(available), 32);
			else
			{
				std::string availableText = LIBRARY->generaltexth->allTexts[217]+ std::to_string(available);
				availableCount = std::make_shared<CLabel>(78, 119, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, availableText);
			}
		}
	}
}

void CFortScreen::RecruitArea::createCreaturePresentation()
{
	const int compactFontHeight = rankedLayout ? rankedFortTinyFontHeight() : 0;
	const int compactTitleCenterY = rankedLayout ? 1 + compactFontHeight / 2 : 0;
	const int compactTitleTop = rankedLayout ? 1 : 0;
	const bool containConfluxPortrait = rankedLayout && town->getFactionID() == FactionID::CONFLUX;
	const int compactPortraitHeight = containConfluxPortrait && compactStatGrid
		? rankedFortConfluxCompactPortraitHeight(cardHeight) : NH_FORT_COMPACT_PORTRAIT_HEIGHT;
	MetaString hoverTextMessage = MetaString::createFromTextID("core.tcommand.21"); // Recruit %s
	hoverTextMessage.replaceNamePlural(getMyCreature()->getId());
	hoverText = hoverTextMessage.toString(&GAME->translator());
	const int creatureX = rankedLayout ? 4 : 159;
	const int creatureY = rankedLayout ? compactTitleTop + compactFontHeight + 1 : 4;
	const bool sidePortrait = containConfluxPortrait && compactStatGrid;
	const auto sideLayout = sidePortrait
		? creatureCardLayout(getMyCreature(), town, level, cardWidth, cardHeight) : CompactCreatureLayout{};
	const int statTop = sidePortrait ? sideLayout.stats.y
		: (rankedLayout ? rankedFortStatTop(compactStatGrid, compactPortraitHeight) : 4);
	if(rankedLayout && (compactStatGrid || containConfluxPortrait))
	{
		const Rect creatureClip = sidePortrait ? sideLayout.portrait : Rect(creatureX, creatureY,
			compactStatGrid ? std::max(1, cardWidth - creatureX - 4) : 150,
			compactStatGrid ? std::max(1, statTop - creatureY) : std::max(1, cardHeight - creatureY - 3));
		createCreaturePreview(creatureClip, getMyCreature(), containConfluxPortrait);
	}
	else
		new CCreaturePic(creatureX, creatureY, getMyCreature(), false);
	new CLabel(rankedLayout ? cardWidth / 2 : 78, rankedLayout ? compactTitleCenterY : 11, rankedLayout ? FONT_TINY : FONT_SMALL,
		ETextAlignment::CENTER, Colors::WHITE, getMyCreature()->getNamePluralTranslated(), rankedLayout ? cardWidth - 8 : 152);
	const auto category = currentCreatureCategory(getMyCreature());
	const auto categoryName = newHorizonsCreatureCategoryUI::name(category, GAME ? &GAME->translator() : nullptr);
	if(!rankedLayout && category && !categoryName.empty())
		categoryLabel = std::make_shared<CLabel>(78, 28, FONT_TINY, ETextAlignment::CENTER,
			Colors::YELLOW, categoryName, 152);

	createStatPresentation(creatureX, statTop, compactPortraitHeight);
}

void CFortScreen::RecruitArea::createStatPresentation(int creatureX, int statTop, int compactPortraitHeight)
{
	auto rankedStatNames = statNames();
	std::array<std::string, NH_FORT_STAT_COUNT> rankedStatDescriptions =
	{
		LIBRARY->generaltexth->translate("core.castinfo.0"),
		LIBRARY->generaltexth->translate("core.castinfo.1"),
		LIBRARY->generaltexth->translate("core.castinfo.2"),
		LIBRARY->generaltexth->translate("core.castinfo.3"),
		LIBRARY->generaltexth->translate("core.castinfo.4"),
		LIBRARY->generaltexth->translate("new-horizons.fort.stat.initiative.description"),
		townGrowthPresentation::description(town->getGrowthInfo(level), getMyCreature()->getId(), &GAME->translator())
	};
	const bool sidePortrait = rankedLayout && compactStatGrid && town->getFactionID() == FactionID::CONFLUX;
	const auto sideLayout = sidePortrait
		? creatureCardLayout(getMyCreature(), town, level, cardWidth, cardHeight) : CompactCreatureLayout{};
	if(sidePortrait && !sideLayout.showNames)
		for(size_t index = 0; index < rankedStatNames.size(); ++index)
		{
			rankedStatDescriptions[index] = rankedStatNames[index] + "\n\n" + rankedStatDescriptions[index];
			rankedStatNames[index].clear();
		}
	const int rowHeight = sidePortrait ? sideLayout.rowStep
		: (rankedLayout ? rankedFortStatRowHeight(cardHeight, compactStatGrid, compactPortraitHeight) : 20);
	if(rankedLayout && !sidePortrait)
		assert(cardHeight >= statTop + rankedFortStatRowCount(compactStatGrid) * rowHeight + rankedFortStatBottomPadding());
	const auto tinyFont = rankedLayout ? ENGINE->renderHandler().loadFont(FONT_TINY) : nullptr;
	const int statLabelWidth = rankedLayout
		? static_cast<int>(std::max({
			tinyFont->getStringWidth(rankedStatNames[0]),
			tinyFont->getStringWidth(rankedStatNames[1]),
			tinyFont->getStringWidth(rankedStatNames[2]),
			tinyFont->getStringWidth(rankedStatNames[3]),
			tinyFont->getStringWidth(rankedStatNames[4]),
			tinyFont->getStringWidth(rankedStatNames[5]),
			tinyFont->getStringWidth(rankedStatNames[6])
		}))
		: 0;
	const int statValueWidth = rankedLayout
		? static_cast<int>(tinyFont->getStringWidth("99999"))
		: 0;
	constexpr int rankedStatIconWidth = 19;
	constexpr int rankedStatIconGap = 2;
	constexpr int rankedStatTextValueGap = 12;
	constexpr int rankedStatRightInset = 12;
	const int minimumStatWidth = rankedStatIconWidth + rankedStatIconGap + statLabelWidth + statValueWidth + rankedStatTextValueGap;
	const int statX = sidePortrait ? sideLayout.stats.x : rankedLayout ? (compactStatGrid ? 4 : std::max(creatureX + 156, cardWidth - minimumStatWidth - rankedStatRightInset)) : 287;
	const int statWidth = sidePortrait ? sideLayout.stats.w : rankedLayout ? (compactStatGrid ? std::max(1, cardWidth - 8) : std::max(1, cardWidth - statX - rankedStatRightInset)) : 96;
	const bool useCompactStatGrid = sidePortrait ? sideLayout.columns == 2 : compactStatGrid;
	const int statColumnGap = useCompactStatGrid ? NH_FORT_CARD_GAP : 0;
	const int statColumnWidth = useCompactStatGrid
		? std::max(1, (statWidth - statColumnGap) / NH_FORT_COMPACT_STAT_COLUMNS) : statWidth;
	const auto rankedStatRect = [statX, statTop, rowHeight, statWidth, useCompactStatGrid, statColumnWidth, statColumnGap](size_t index)
	{
		if(!useCompactStatGrid)
			return Rect(statX, statTop + static_cast<int>(index) * rowHeight, statWidth, rowHeight);
		return Rect(statX + static_cast<int>(index % NH_FORT_COMPACT_STAT_COLUMNS) * (statColumnWidth + statColumnGap),
			statTop + static_cast<int>(index / NH_FORT_COMPACT_STAT_COLUMNS) * rowHeight,
			statColumnWidth, rowHeight);
	};
	Rect sizes(statX, statTop, statWidth, rowHeight);
	if(rankedLayout)
	{
		const std::array<ImagePath, NH_FORT_STAT_COUNT> statIconPaths =
		{
			ImagePath::builtin("stackWindow/iconAttack"),
			ImagePath::builtin("stackWindow/iconDefense"),
			ImagePath::builtin("stackWindow/iconDamage"),
			ImagePath::builtin("stackWindow/iconHealth"),
			ImagePath::builtin("stackWindow/iconSpeed"),
			ImagePath::builtin("stackWindow/iconInitiative"),
			ImagePath::builtin("stackWindow/iconGrowth")
		};
		for(size_t index = 0; index < statIconPaths.size(); ++index)
		{
			const auto rect = rankedStatRect(index);
			const int iconSize = std::max(1, std::min(rankedStatIconWidth, rect.h - 1));
			const int iconX = rect.x + (rankedStatIconWidth - iconSize) / 2;
			const int iconY = rect.y + (rect.h - iconSize) / 2;
			auto icon = std::make_shared<CPicture>(statIconPaths[index], iconX, iconY);
			icon->scaleTo(Point(iconSize, iconSize));
			rankedStatIcons.push_back(icon);
		}
		const auto rankedValueRect = [&rankedStatRect, sidePortrait](size_t index)
		{
			auto rect = rankedStatRect(index);
			rect.x += rankedStatIconWidth + rankedStatIconGap;
			rect.w = std::max(1, rect.w - rankedStatIconWidth - rankedStatIconGap);
			// LabeledValue places numbers two pixels above its bottom edge.
			// Keep them aligned with names when tight rows use the font's exact height.
			if(sidePortrait)
				rect.h += 2;
			return rect;
		};
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(0), rankedStatNames[0], rankedStatDescriptions[0], getMyCreature()->getAttack(false), true));
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(1), rankedStatNames[1], rankedStatDescriptions[1], getMyCreature()->getDefense(false), true));
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(2), rankedStatNames[2], rankedStatDescriptions[2], getMyCreature()->getMinDamage(false), getMyCreature()->getMaxDamage(false), true));
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(3), rankedStatNames[3], rankedStatDescriptions[3], getMyCreature()->getMaxHealth(), true));
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(4), rankedStatNames[4], rankedStatDescriptions[4], getMyCreature()->getBaseSpeed(), true));
		values.push_back(std::make_shared<LabeledValue>(rankedValueRect(5), rankedStatNames[5], rankedStatDescriptions[5], getMyCreature()->getBaseInitiative(), true));

		if(town->getFactionID() == FactionID::CONFLUX && town->creatures[level].second.empty())
			values.push_back(std::make_shared<LabeledValue>(rankedValueRect(6), rankedStatNames[6],
				rankedStatDescriptions[6], std::string("0"), true));
		else
			values.push_back(std::make_shared<LabeledValue>(rankedValueRect(6), rankedStatNames[6],
				rankedStatDescriptions[6], town->creatureGrowth(level), true));
	}
	else
	{
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[190], LIBRARY->generaltexth->translate("core.castinfo.0"), getMyCreature()->getAttack(false)));
		sizes.y+=20;
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[191], LIBRARY->generaltexth->translate("core.castinfo.1"), getMyCreature()->getDefense(false)));
		sizes.y+=21;
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[199], LIBRARY->generaltexth->translate("core.castinfo.2"), getMyCreature()->getMinDamage(false), getMyCreature()->getMaxDamage(false)));
		sizes.y+=20;
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[388], LIBRARY->generaltexth->translate("core.castinfo.3"), getMyCreature()->getMaxHealth()));
		sizes.y+=21;
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[193], LIBRARY->generaltexth->translate("core.castinfo.4"), getMyCreature()->valOfBonuses(BonusType::STACKS_SPEED)));
		sizes.y+=20;
		values.push_back(std::make_shared<LabeledValue>(sizes, LIBRARY->generaltexth->allTexts[194], townGrowthPresentation::description(town->getGrowthInfo(level), getMyCreature()->getId(), &GAME->translator()), town->creatureGrowth(level)));
	}
}

void CFortScreen::RecruitArea::show(Canvas & to)
{
	if(rankedLayout)
	{
		CanvasClipRectGuard clip(to, pos);
		CIntObject::show(to);
	}
	else
		CIntObject::show(to);
}

void CFortScreen::RecruitArea::showAll(Canvas & to)
{
	if(rankedLayout)
	{
		CanvasClipRectGuard clip(to, pos);
		CIntObject::showAll(to);
	}
	else
		CIntObject::showAll(to);
}

const CCreature * CFortScreen::RecruitArea::getMyCreature()
{
	if(!town->creatures.at(level).second.empty()) // built
		return town->creatures.at(level).second.back().toCreature();
	if(!town->getTown()->creatures.at(level).empty()) // there are creatures on this level
		return town->getTown()->creatures.at(level).front().toCreature();
	return nullptr;
}

const CBuilding * CFortScreen::RecruitArea::getMyBuilding()
{
	BuildingID myID = BuildingID(BuildingID::getDwellingFromLevel(level, 0));

	if (level == town->getTown()->creatures.size())
		return town->getTown()->getSpecialBuilding(BuildingSubID::PORTAL_OF_SUMMONING);

	if (!town->getTown()->buildings.count(myID))
		return nullptr;

	const CBuilding * build = town->getTown()->buildings.at(myID).get();
	while (town->getTown()->buildings.count(myID))
	{
		if (town->hasBuilt(myID))
			build = town->getTown()->buildings.at(myID).get();
		BuildingID::advanceDwelling(myID);
	}

	return build;
}

void CFortScreen::RecruitArea::hover(bool on)
{
	if(on)
		ENGINE->statusbar()->write(hoverText);
	else
		ENGINE->statusbar()->clear();
}

void CFortScreen::RecruitArea::creaturesChangedEventHandler()
{
	if(availableCount)
	{
		std::string availableText = rankedLayout
			? std::to_string(town->creatures[level].first)
			: LIBRARY->generaltexth->allTexts[217] + std::to_string(town->creatures[level].first);
		availableCount->setText(availableText);
	}
}

void CFortScreen::RecruitArea::clickPressed(const Point & cursorPosition)
{
	GAME->interface()->castleInt->builds->enterDwelling(level);
}

void CFortScreen::RecruitArea::showPopupWindow(const Point & cursorPosition)
{
	if (getMyCreature() != nullptr)
		ENGINE->windows().createAndPushWindow<CStackWindow>(getMyCreature(), true);
}
