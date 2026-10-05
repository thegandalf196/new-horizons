/*
 * HeroInfoWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../windows/SpellPointPresentation.h"
#include "HeroInfoWindow.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"

#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../windows/InfoWindows.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../render/IRenderHandler.h"
#include "../render/IFont.h"

namespace
{
static_assert(HeroInfoPanelLayout::effectAreaWidth == 70,
	"Compact hero battle status entries must retain the existing 70px width");
static_assert(HeroInfoPanelLayout::effectAreaRowHeight >= HeroInfoPanelLayout::effectAreaIconSize + 2 * 2,
	"Combat status entries must retain an inset for their optional icons");
static_assert(HeroInfoPanelLayout::heroActionStatePanelHeight == HeroInfoPanelLayout::heroActionStateHeaderHeight
	+ HeroInfoPanelLayout::heroActionStateLineHeight + HeroInfoPanelLayout::heroActionStatePanelPadding,
	"Normal Hero Action status must remain a compact two-line state");
static_assert(HeroInfoPanelLayout::heroActionStateHeaderHeight
	+ HeroInfoPanelLayout::heroActionStateLineHeight / 2 < HeroInfoPanelLayout::heroActionStatePanelHeight,
	"Normal Hero Action status text must remain within its panel");
static_assert(HeroInfoPanelLayout::effectAreaLeft - HeroInfoPanelLayout::backgroundInset >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::width
	- (HeroInfoPanelLayout::effectAreaLeft + HeroInfoPanelLayout::effectAreaWidth) >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(
	HeroInfoPanelLayout::effectAreaTop - (HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::height) >= 3,
	"Hero effect area must remain below the portrait frame with an internal gap");
static_assert(
	HeroInfoPanelLayout::effectAreaWidth >= 23 + 42 + 5,
	"Icon-backed status text and value must fit without overlapping the frame");

std::string fitStatusRowText(const std::string & text, int maxWidth)
{
	const auto & font = ENGINE->renderHandler().loadFont(EFonts::FONT_TINY);
	if(font->getStringWidth(text) <= maxWidth)
		return text;

	std::string shortened = text;
	constexpr std::string_view ellipsis = "...";
	while(!shortened.empty() && font->getStringWidth(shortened + std::string(ellipsis)) > maxWidth)
		TextOperations::trimRightUnicode(shortened);
	return shortened.empty() ? std::string(ellipsis) : shortened + std::string(ellipsis);
}
}

HeroBattleStatusArea::HeroBattleStatusArea(const Point & position)
	: CIntObject(0, position)
{
	setRedrawParent(true);
	pos.w = HeroInfoPanelLayout::effectAreaWidth;
	pos.h = 0;
}

void HeroBattleStatusArea::setStatus(const std::vector<CombatStatusEntry> & entries,
	bool normalHeroActionAvailable_, bool showNormalHeroAction_)
{
	if(statusEntries == entries && normalHeroActionAvailable == normalHeroActionAvailable_
		&& showNormalHeroAction == showNormalHeroAction_)
		return;

	if(!statusbarText.empty())
		ENGINE->statusbar()->clearIfMatching(statusbarText);

	OBJECT_CONSTRUCTION;
	statusEntries = entries;
	normalHeroActionAvailable = normalHeroActionAvailable_;
	showNormalHeroAction = showNormalHeroAction_;
	refreshContents();
}

int HeroBattleStatusArea::statusHeight() const
{
	return pos.h;
}

void HeroBattleStatusArea::addFramedBackground(const Rect & bounds)
{
	// Reuse the battle UI's original leather texture, with the red inset and
	// fine gold edging of the adjacent hero-card compartments.
	textures.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), bounds));
	const ColorRGBA transparent(0, 0, 0, 0);
	backgrounds.push_back(std::make_shared<TransparentFilledRectangle>(bounds,
		transparent, ColorRGBA(213, 185, 117)));
	backgrounds.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(bounds.x + 1, bounds.y + 1, bounds.w - 2, bounds.h - 2),
		transparent, ColorRGBA(145, 18, 12), 2));
	backgrounds.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(bounds.x + 3, bounds.y + 3, bounds.w - 6, bounds.h - 6),
		transparent, ColorRGBA(120, 98, 56)));
}

void HeroBattleStatusArea::refreshContents()
{
	OBJECT_CONSTRUCTION;
	const bool wasVisible = hasVisibleStatus;
	textures.clear();
	backgrounds.clear();
	statusIcons.clear();
	labels.clear();
	statusbarText.clear();
	helpText.clear();

	const int statusRows = static_cast<int>(statusEntries.size());
	hasVisibleStatus = !statusEntries.empty() || showNormalHeroAction;
	pos.h = statusRows * HeroInfoPanelLayout::effectAreaRowHeight
		+ (showNormalHeroAction ? HeroInfoPanelLayout::heroActionStatePanelHeight : 0);
	if(!hasVisibleStatus)
	{
		removeUsedEvents(HOVER | SHOW_POPUP);
		if(wasVisible)
			ENGINE->windows().totalRedraw();
		return;
	}

	addUsedEvents(HOVER | SHOW_POPUP);

	for(size_t row = 0; row < statusEntries.size(); ++row)
	{
		const auto & entry = statusEntries[row];
		const int rowY = static_cast<int>(row) * HeroInfoPanelLayout::effectAreaRowHeight;
		addFramedBackground(Rect(0, rowY,
			HeroInfoPanelLayout::effectAreaWidth, HeroInfoPanelLayout::effectAreaRowHeight));
		const bool hasIcon = !entry.icon.empty();
		const int textLeft = hasIcon ? 23 : 7;
		const int textWidth = hasIcon ? 42 : 56;
		if(hasIcon)
		{
			auto icon = std::make_shared<CPicture>(ImagePath::builtin(entry.icon), Point(5, rowY + 8));
			icon->scaleTo(Point(HeroInfoPanelLayout::effectAreaIconSize, HeroInfoPanelLayout::effectAreaIconSize));
			statusIcons.push_back(std::move(icon));
		}

		const auto label = fitStatusRowText(entry.label, textWidth);
		const auto value = fitStatusRowText(entry.value, textWidth);
		labels.push_back(std::make_shared<CLabel>(textLeft, rowY + 5, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
			Colors::YELLOW, label, textWidth));
		labels.push_back(std::make_shared<CLabel>(textLeft, rowY + 17, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
			Colors::WHITE, value, textWidth));

		if(!entry.tooltip.empty())
		{
			if(!helpText.empty())
				helpText += "\n\n";
			helpText += entry.tooltip;
		}
		if(!entry.label.empty() || !entry.value.empty())
		{
			if(!statusbarText.empty())
				statusbarText += "  ";
			statusbarText += entry.label + (entry.value.empty() ? "." : ": " + entry.value + ".");
		}
	}

	if(showNormalHeroAction)
	{
		const int stateTop = statusRows * HeroInfoPanelLayout::effectAreaRowHeight;
		addFramedBackground(Rect(0, stateTop,
			HeroInfoPanelLayout::effectAreaWidth, HeroInfoPanelLayout::heroActionStatePanelHeight));
		labels.push_back(std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth / 2,
			stateTop + 11, EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW, "Hero Action"));
		const auto stateText = normalHeroActionAvailable ? "Available" : "Spent";
		const auto stateColor = normalHeroActionAvailable ? Colors::YELLOW : Colors::WHITE;
		labels.push_back(std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth / 2,
			stateTop + HeroInfoPanelLayout::heroActionStateHeaderHeight
				+ HeroInfoPanelLayout::heroActionStateLineHeight / 2,
			EFonts::FONT_TINY, ETextAlignment::CENTER, stateColor, stateText));

		const auto actionHelp = CInfoWindow::genText("Hero Action",
			std::string("Normal Hero Action: ") + stateText + "."
			+ " Additional Spell-only and Order-only opportunities are shown with their source and expiry at the ordinary casting and Orders controls.");
		if(helpText.empty())
			helpText = actionHelp;
		else
			helpText += "\n\n" + actionHelp;
		if(!statusbarText.empty())
			statusbarText += "  ";
		statusbarText += std::string("Hero Action: ") + stateText + ".";
	}

	ENGINE->windows().totalRedraw();
}

void HeroBattleStatusArea::setRenderDuringShow(bool value)
{
	renderDuringShow = value;
}

void HeroBattleStatusArea::hover(bool on)
{
	if(statusbarText.empty())
		return;
	if(on)
		ENGINE->statusbar()->write(statusbarText);
	else
		ENGINE->statusbar()->clearIfMatching(statusbarText);
}

void HeroBattleStatusArea::showPopupWindow(const Point &)
{
	if(!helpText.empty())
		CRClickPopup::createAndPush(helpText);
}

void HeroBattleStatusArea::showAll(Canvas & to)
{
	if(hasVisibleStatus)
		CIntObject::showAll(to);
}

void HeroBattleStatusArea::show(Canvas & to)
{
	if(renderDuringShow && hasVisibleStatus)
		showAll(to);
}

HeroInfoBasicPanel::HeroInfoBasicPanel(const InfoAboutHero & hero, const Point * position, bool initializeBackground,
	bool showBattleStatus_)
	: BattleSidePanel(0)
	, showBattleStatus(showBattleStatus_)
{
	OBJECT_CONSTRUCTION;
	if(position != nullptr)
		moveTo(*position);

	if(initializeBackground)
	{
		background = std::make_shared<CPicture>(ImagePath::builtin("CHRPOP"),
			Rect(HeroInfoPanelLayout::backgroundInset, HeroInfoPanelLayout::backgroundInset,
				HeroInfoPanelLayout::width - HeroInfoPanelLayout::backgroundInset, HeroInfoPanelLayout::height),
			HeroInfoPanelLayout::backgroundInset, HeroInfoPanelLayout::backgroundInset);
		background->setPlayerColor(hero.owner);
	}

	initializeData(hero);
}

void HeroInfoBasicPanel::initializeData(const InfoAboutHero & hero)
{
	OBJECT_CONSTRUCTION;
	if(showBattleStatus && !battleStatus)
		battleStatus = std::make_shared<HeroBattleStatusArea>(
			Point(HeroInfoPanelLayout::effectAreaLeft, HeroInfoPanelLayout::effectAreaTop));

	auto attack = hero.details->primskills[0];
	auto defense = hero.details->primskills[1];
	auto power = hero.details->primskills[2];
	auto knowledge = hero.details->primskills[3];
	auto morale = hero.details->morale;
	auto luck = hero.details->luck;
	auto currentSpellPoints = hero.details->mana;
	auto maxSpellPoints = hero.details->manaLimit;

	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), hero.getIconIndex(), 0, 10, 6));

	//primary stats
	labels.push_back(std::make_shared<CLabel>(9, 75, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[380] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 87, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[381] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 99, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[382] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 111, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[383] + ":"));

	labels.push_back(std::make_shared<CLabel>(69, 87, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(attack)));
	labels.push_back(std::make_shared<CLabel>(69, 99, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(defense)));
	labels.push_back(std::make_shared<CLabel>(69, 111, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(power)));
	labels.push_back(std::make_shared<CLabel>(69, 123, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(knowledge)));

	//morale+luck
	labels.push_back(std::make_shared<CLabel>(9, 131, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[384] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 143, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[385] + ":"));

	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("IMRL22"), std::clamp(morale + 3, 0, 6), 0, 47, 131));
	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ILCK22"), std::clamp(luck + 3, 0, 6), 0, 47,
		HeroInfoPanelLayout::luckIconY));

	//spell points
	const bool hasBuffer = hero.details->bufferMana > 0;
	const auto spellPointsText = std::to_string(currentSpellPoints)
		+ (maxSpellPoints >= 0 ? "/" + std::to_string(maxSpellPoints) : "");
	labels.push_back(std::make_shared<CLabel>(39, HeroInfoPanelLayout::spellPointsLabelY - (hasBuffer ? 4 : 0), EFonts::FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[387]));
	labels.push_back(std::make_shared<CLabel>(39, HeroInfoPanelLayout::spellPointsValueY - (hasBuffer ? 4 : 0), EFonts::FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, spellPointsText, 70));
	// Keep the Buffer annotation on its own line in this narrow panel. Trimming
	// an inline colored string can cut its markup as well as hide the amount.
	if(hasBuffer)
		labels.push_back(std::make_shared<CLabel>(39, HeroInfoPanelLayout::spellPointsValueY + 8, EFonts::FONT_TINY,
			ETextAlignment::CENTER, ColorRGBA(0, 191, 255), "+" + std::to_string(hero.details->bufferMana), 70));
	spellPointsArea = std::make_shared<LRClickableAreaWText>(Rect(3, 166, 70, 33), LIBRARY->generaltexth->allTexts[387],
		spellPointPresentation::tooltip(currentSpellPoints, maxSpellPoints, hero.details->bufferMana));

}

void HeroInfoBasicPanel::update(const InfoAboutHero & updatedInfo)
{
	icons.clear();
	labels.clear();

	initializeData(updatedInfo);
	redraw();
}

void HeroInfoBasicPanel::setBattleStatus(const std::vector<CombatStatusEntry> & entries,
	bool normalHeroActionAvailable, bool showNormalHeroAction)
{
	if(!showBattleStatus || !battleStatus)
		return;
	battleStatus->setStatus(entries, normalHeroActionAvailable, showNormalHeroAction);
}

int HeroInfoBasicPanel::battleStatusHeight() const
{
	return battleStatus ? battleStatus->statusHeight() : 0;
}

void HeroInfoBasicPanel::setBattleStatusRenderDuringShow(bool value)
{
	if(battleStatus)
		battleStatus->setRenderDuringShow(value);
}

HeroInfoWindow::HeroInfoWindow(const InfoAboutHero & hero, const Point * position)
	: CWindowObject(RCLICK_POPUP | SHADOW_DISABLED, ImagePath::builtin("CHRPOP"))
{
	OBJECT_CONSTRUCTION;
	if(position != nullptr)
		moveTo(*position);

	background->setPlayerColor(hero.owner); //maybe add this functionality to base class?

	content = std::make_shared<HeroInfoBasicPanel>(hero, nullptr, false);
}
