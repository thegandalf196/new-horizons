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
#include "HeroInfoWindow.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"

#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
static_assert(
	HeroInfoPanelLayout::effectAreaLeft - HeroInfoPanelLayout::backgroundInset >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(
	HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::width
		- (HeroInfoPanelLayout::effectAreaLeft + HeroInfoPanelLayout::effectAreaWidth) >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(
	HeroInfoPanelLayout::effectAreaTop - (HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::height) >= 3,
	"Hero effect area must remain below the portrait frame with an internal gap");
static_assert(
	HeroInfoPanelLayout::effectAreaHeight >= HeroInfoPanelLayout::effectAreaIconSize + 2 * 2,
	"Hero effect area must retain an inset for standard 22px status icons");
static_assert(
	HeroInfoPanelLayout::counterspellStatusY - HeroInfoPanelLayout::effectAreaTop
		>= HeroInfoPanelLayout::effectAreaTextLineAllowance / 2 + HeroInfoPanelLayout::effectAreaTextMargin,
	"Hero effect text needs a clear top margin");
static_assert(
	HeroInfoPanelLayout::effectAreaTop + HeroInfoPanelLayout::effectAreaHeight - HeroInfoPanelLayout::counterspellStatusY
		>= HeroInfoPanelLayout::effectAreaTextLineAllowance / 2 + HeroInfoPanelLayout::effectAreaTextMargin,
	"Hero effect text needs a clear bottom margin");
static_assert(
	HeroInfoPanelLayout::outsideStackPanelOffsetY
		>= HeroInfoPanelLayout::effectAreaTop + HeroInfoPanelLayout::effectAreaHeight + 3,
	"Outside stack panel must not overlap the hero effect row");
}

HeroCounterspellStatusArea::HeroCounterspellStatusArea(const Point & position, bool armed_)
	: CIntObject(0, position)
{
	setRedrawParent(true);
	setArmed(armed_);
}

void HeroCounterspellStatusArea::setArmed(bool value)
{
	if(armed == value)
		return;

	OBJECT_CONSTRUCTION;
	armed = value;
	background.reset();
	label.reset();
	if(armed)
	{
		background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, HeroInfoPanelLayout::effectAreaWidth,
			HeroInfoPanelLayout::effectAreaHeight), ColorRGBA(0, 0, 0, 75), ColorRGBA(128, 100, 75));
		label = std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth / 2,
			HeroInfoPanelLayout::effectAreaHeight / 2, EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW,
			"Ward: ARMED");
	}
	if(armed)
		redraw();
	else
	{
		// The effect strip is outside the 200px portrait background; repaint the battlefield under it.
		ENGINE->windows().totalRedraw();
	}
}

void HeroCounterspellStatusArea::setRenderDuringShow(bool value)
{
	renderDuringShow = value;
}

void HeroCounterspellStatusArea::show(Canvas & to)
{
	if(renderDuringShow)
		showAll(to);
}

HeroInfoBasicPanel::HeroInfoBasicPanel(const InfoAboutHero & hero, const Point * position, bool initializeBackground,
	bool showCounterspellStatus_, bool counterspellArmed_)
	: BattleSidePanel(0)
	, showCounterspellStatus(showCounterspellStatus_)
	, counterspellArmed(counterspellArmed_)
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
	if(showCounterspellStatus && !counterspellStatus)
	{
		counterspellStatus = std::make_shared<HeroCounterspellStatusArea>(
			Point(HeroInfoPanelLayout::effectAreaLeft, HeroInfoPanelLayout::effectAreaTop), counterspellArmed);
		// This row is painted by the enclosing hero panel's full redraw path.
		counterspellStatus->setRenderDuringShow(false);
	}

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
	const auto spellPointsText = std::to_string(currentSpellPoints) + "/" + std::to_string(maxSpellPoints);
	labels.push_back(std::make_shared<CLabel>(39, HeroInfoPanelLayout::spellPointsLabelY, EFonts::FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[387]));
	labels.push_back(std::make_shared<CLabel>(39, HeroInfoPanelLayout::spellPointsValueY, EFonts::FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, spellPointsText));

}

void HeroInfoBasicPanel::update(const InfoAboutHero & updatedInfo, std::optional<bool> counterspellArmed_)
{
	icons.clear();
	labels.clear();
	if(counterspellArmed_.has_value())
		counterspellArmed = *counterspellArmed_;
	if(counterspellStatus)
		counterspellStatus->setArmed(counterspellArmed);

	initializeData(updatedInfo);
	redraw();
}

void HeroInfoBasicPanel::setCounterspellStatus(bool armed)
{
	OBJECT_CONSTRUCTION;
	if(!showCounterspellStatus || counterspellArmed == armed)
		return;
	counterspellArmed = armed;
	if(counterspellStatus)
		counterspellStatus->setArmed(counterspellArmed);
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
