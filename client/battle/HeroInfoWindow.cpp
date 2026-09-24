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

namespace
{
static_assert(HeroInfoPanelLayout::effectAreaWidth == 70,
	"Compact hero battle status rows must retain the existing 70px width");
static_assert(HeroInfoPanelLayout::effectAreaRowHeight == 32
	&& HeroInfoPanelLayout::effectAreaHeight == HeroInfoPanelLayout::effectAreaRowHeight * 2
		+ HeroInfoPanelLayout::actionCountPanelHeight,
	"Counterspell and Warcasting rows must stay separate from the three-line allowance panel");
static_assert(HeroInfoPanelLayout::actionCountPanelHeight == HeroInfoPanelLayout::actionCountHeaderHeight
	+ HeroInfoPanelLayout::actionCountLineHeight * 3 + HeroInfoPanelLayout::actionCountPanelPadding,
	"Each hero allowance count needs its own readable line");
static_assert(HeroInfoPanelLayout::effectAreaLeft - HeroInfoPanelLayout::backgroundInset >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::width
	- (HeroInfoPanelLayout::effectAreaLeft + HeroInfoPanelLayout::effectAreaWidth) >= 3,
	"Hero effect area must keep a side margin from the portrait frame");
static_assert(
	HeroInfoPanelLayout::effectAreaTop - (HeroInfoPanelLayout::backgroundInset + HeroInfoPanelLayout::height) >= 3,
	"Hero effect area must remain below the portrait frame with an internal gap");
static_assert(
	HeroInfoPanelLayout::effectAreaRowHeight >= HeroInfoPanelLayout::effectAreaIconSize + 2 * 2,
	"Hero effect area must retain an inset for the Warcasting icon");
static_assert(HeroInfoPanelLayout::compactAttackerEffectAreaLeft + HeroInfoPanelLayout::effectAreaWidth <= 800
	&& HeroInfoPanelLayout::compactDefenderEffectAreaLeft + HeroInfoPanelLayout::effectAreaWidth <= 800,
	"Compact hero status rows must stay inside the 800px battle reference width");
static_assert(
	HeroInfoPanelLayout::outsideStackPanelOffsetY
		>= HeroInfoPanelLayout::effectAreaTop + HeroInfoPanelLayout::effectAreaHeight + 3,
	"Outside stack panel must not overlap the hero effect row");

bool hasActiveWarcasting(const AlternatingHeroActionState & state, int round)
{
	return state.bonusFor(state.nextEligibleAction, round) > 0;
}

std::string warcastingIconName(AlternatingHeroActionState::Action action)
{
	if(action == AlternatingHeroActionState::Action::SPELL)
		return "NH_perk_arcane_channeling_normal.png";
	if(action == AlternatingHeroActionState::Action::ORDER)
		return "NH_perk_martial_channeling_normal.png";
	return {};
}
}

HeroBattleStatusArea::HeroBattleStatusArea(const Point & position)
	: CIntObject(0, position)
{
	setRedrawParent(true);
	pos.w = HeroInfoPanelLayout::effectAreaWidth;
	pos.h = 0;
}

void HeroBattleStatusArea::setStatus(bool counterspellIsArmed, const AlternatingHeroActionState & warcasting,
	const HeroActionAllowanceState::Counts & actionCounts, bool showActionCounts_, int round)
{
	if(counterspellArmed == counterspellIsArmed && warcastingState == warcasting
		&& actionCounts == this->actionCounts && showActionCounts == showActionCounts_ && currentRound == round)
		return;

	if(!statusbarText.empty())
		ENGINE->statusbar()->clearIfMatching(statusbarText);

	OBJECT_CONSTRUCTION;
	counterspellArmed = counterspellIsArmed;
	warcastingState = warcasting;
	this->actionCounts = actionCounts;
	showActionCounts = showActionCounts_;
	currentRound = round;
	refreshContents();
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
	warcastingIcon.reset();
	labels.clear();
	statusbarText.clear();
	helpText.clear();

	const bool warcastingActive = hasActiveWarcasting(warcastingState, currentRound);
	const int statusRows = static_cast<int>(counterspellArmed) + static_cast<int>(warcastingActive);
	hasVisibleStatus = statusRows > 0 || showActionCounts;
	pos.h = statusRows * HeroInfoPanelLayout::effectAreaRowHeight
		+ (showActionCounts ? HeroInfoPanelLayout::actionCountPanelHeight : 0);
	if(!hasVisibleStatus)
	{
		removeUsedEvents(HOVER | SHOW_POPUP);
		if(wasVisible)
			ENGINE->windows().totalRedraw();
		return;
	}

	addUsedEvents(HOVER | SHOW_POPUP);

	if(warcastingActive)
	{
		const auto action = warcastingState.nextEligibleAction;
		const auto empowerment = warcastingState.bonusFor(action, currentRound);
		const auto actionName = action == AlternatingHeroActionState::Action::SPELL ? "Spell" : "Order";
		const auto amount = action == AlternatingHeroActionState::Action::SPELL
			? "+" + std::to_string(empowerment) + "%"
			: "+" + std::to_string(empowerment) + "pp";
		const auto warcastingRowY = counterspellArmed ? HeroInfoPanelLayout::effectAreaRowHeight : 0;

		addFramedBackground(Rect(0, warcastingRowY,
			HeroInfoPanelLayout::effectAreaWidth, HeroInfoPanelLayout::effectAreaRowHeight));
		warcastingIcon = std::make_shared<CPicture>(ImagePath::builtin(warcastingIconName(action)),
			Point(5, warcastingRowY + 8));
		warcastingIcon->scaleTo(Point(HeroInfoPanelLayout::effectAreaIconSize, HeroInfoPanelLayout::effectAreaIconSize));
		labels.push_back(std::make_shared<CLabel>(23, warcastingRowY + 5, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
			Colors::YELLOW, actionName, 42));
		labels.push_back(std::make_shared<CLabel>(23, warcastingRowY + 17, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
			Colors::WHITE, amount, 42));

		const auto actionEffect = action == AlternatingHeroActionState::Action::SPELL
			? "to its Spell Power-derived numerical component."
			: "to efficiency on attribute-derived components.";
		const auto amountDescription = action == AlternatingHeroActionState::Action::SPELL
			? "+" + std::to_string(empowerment) + "%"
			: "+" + std::to_string(empowerment) + " percentage points";
		helpText = CInfoWindow::genText("Warcasting",
			std::string("Next eligible action: ") + actionName + ". It gains " + amountDescription + " " + actionEffect
			+ " Available through round " + std::to_string(warcastingState.expiryRound) + " (inclusive).");
		statusbarText = std::string("Warcasting: next ") + actionName + " " + amount
			+ " through round " + std::to_string(warcastingState.expiryRound) + " (inclusive).";
	}

	if(counterspellArmed)
	{
		addFramedBackground(Rect(0, 0,
			HeroInfoPanelLayout::effectAreaWidth, HeroInfoPanelLayout::effectAreaRowHeight));
		labels.push_back(std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth / 2,
			HeroInfoPanelLayout::effectAreaRowHeight / 2, EFonts::FONT_TINY, ETextAlignment::CENTER,
			Colors::YELLOW, "Ward: ARMED"));

		const auto wardHelp = CInfoWindow::genText("Counterspell Ward", "The Counterspell ward is armed for this side.");
		if(helpText.empty())
			helpText = wardHelp;
		else
			helpText += "\n\n" + wardHelp;
		if(!statusbarText.empty())
			statusbarText += "  ";
		statusbarText += "Counterspell ward: armed.";
	}

	if(showActionCounts)
	{
		const int countsTop = statusRows * HeroInfoPanelLayout::effectAreaRowHeight;
		addFramedBackground(Rect(0, countsTop,
			HeroInfoPanelLayout::effectAreaWidth, HeroInfoPanelLayout::actionCountPanelHeight));
		labels.push_back(std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth / 2,
			countsTop + 11, EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW, "Actions"));
		const auto addCount = [&](int row, const std::string & name, int count)
		{
			const int y = countsTop + HeroInfoPanelLayout::actionCountHeaderHeight
				+ row * HeroInfoPanelLayout::actionCountLineHeight;
			labels.push_back(std::make_shared<CLabel>(7, y, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
				Colors::WHITE, name));
			labels.push_back(std::make_shared<CLabel>(HeroInfoPanelLayout::effectAreaWidth - 7,
				y + HeroInfoPanelLayout::actionCountLineHeight - 2, EFonts::FONT_TINY,
				ETextAlignment::BOTTOMRIGHT, count > 0 ? Colors::YELLOW : Colors::WHITE, std::to_string(count)));
		};
		addCount(0, "Hero", actionCounts.heroActions);
		addCount(1, "Order", actionCounts.orderActions);
		addCount(2, "Spell", actionCounts.spellActions);

		const auto countsHelp = CInfoWindow::genText("Hero Action Allowances",
			"Hero Actions: " + std::to_string(actionCounts.heroActions)
			+ "\nOrder Actions: " + std::to_string(actionCounts.orderActions)
			+ "\nSpell Actions: " + std::to_string(actionCounts.spellActions)
			+ "\nHero Actions can cast a spell OR issue an Order."
			+ " Spell Actions can only cast spells; Order Actions can only issue Orders."
			+ " Metamagic Spell Actions remain available until used or expired, and creature actions do not spend them.");
		if(helpText.empty())
			helpText = countsHelp;
		else
			helpText += "\n\n" + countsHelp;
		if(!statusbarText.empty())
			statusbarText += "  ";
		statusbarText += "Hero / Order / Spell Actions: " + std::to_string(actionCounts.heroActions) + " / "
			+ std::to_string(actionCounts.orderActions) + " / " + std::to_string(actionCounts.spellActions) + ".";
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

void HeroInfoBasicPanel::setBattleStatus(bool counterspellIsArmed, const AlternatingHeroActionState & warcasting,
	const HeroActionAllowanceState::Counts & actionCounts, bool showActionCounts, int round)
{
	if(!showBattleStatus || !battleStatus)
		return;
	battleStatus->setStatus(counterspellIsArmed, warcasting, actionCounts, showActionCounts, round);
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
