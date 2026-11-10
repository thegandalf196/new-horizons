/*
 * CHeroWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "HeroWindowLabelText.h"
#include "../GameEngine.h"
#include "../gui/TextAlignment.h"
#include "../gui/Shortcut.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/CComponent.h"
#include "../widgets/CGarrisonInt.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"

void CHeroWindow::configureNewHorizonsLayout()
{
	const auto move = [this](const auto & widget, Point point)
	{
		widget->moveTo(pos.topLeft() + point);
	};
	banner->disable();
	move(name, Point(139, 32));
	name->setMaxWidth(114);
	move(title, Point(152, 61));
	title->setMaxWidth(140);
	move(portraitImage, Point(16, 18));
	move(portraitArea, Point(16, 18));
	move(portraitWikiArea, Point(16, 18));
	move(quitButton, Point(740, 570));
	move(backpackButton, Point(444, 480));
	move(questlogButton, Point(502, 480));
	move(dismissButton, Point(618, 480));
	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(0, 608, 800, 16), 0, 608));

	for(size_t i = 0; i < primSkillAreas.size(); ++i)
	{
		const int x = 236 + static_cast<int>(i) * 82;
		move(primSkillAreas[i], Point(x, 12));
		primSkillAreas[i]->pos.w = 82;
		primSkillAreas[i]->pos.h = 76;
		move(primSkillValues[i], Point(x + 40, 80));
		move(primSkillImages[i == 3 ? 5 : i], Point(x + 20, 30));
		move(labels[i], Point(x + 41, 21));
		labels[i]->setMaxWidth(74);
		labels[i]->setText(labels[i]->getText());
		growthValues.push_back(std::make_shared<CLabel>(x + 54, 80, FONT_SMALL, ETextAlignment::CENTER, Colors::GREEN, "", 32));
	}
	move(primSkillImages[3], Point(294, 88));
	move(primSkillImages[4], Point(14, 132));
	move(specImage, Point(14, 88));
	move(specArea, Point(12, 88));
	move(expArea, Point(12, 132));
	move(spellPointsArea, Point(292, 88));
	// Three compact native glyphs share the leather band ending at728.
	// The blue hero-list strip starts at734 and is never a status cell.
	morale->small = true;
	luck->small = true;
	morale->centeredReadout = true;
	luck->centeredReadout = true;
	move(morale, Point(576, 30));
	morale->pos.w = 30;
	morale->pos.h = 58;
	move(luck, Point(631, 30));
	luck->pos.w = 30;
	luck->pos.h = 58;
	move(labels[4], Point(62, 92));
	move(labels[5], Point(62, 136));
	move(labels[6], Point(342, 92));
	for(size_t i = 4; i < 7; ++i)
	{
		labels[i]->alignment = ETextAlignment::TOPCENTER;
		move(labels[i], Point(i == 6 ? 342 : 62, i == 5 ? 136 : 92));
		// Reused TOPLEFT labels retain their old measured width. Give centered
		// text the actual pane, rather than adding that width to a center anchor.
		labels[i]->pos.w = 88;
		labels[i]->pos.h = 0;
		labels[i]->setMaxWidth(88);
		labels[i]->setText(labels[i]->getText());
	}
	specName = std::make_shared<CLabel>(106, 110, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 88);
	expValue = std::make_shared<CLabel>(106, 154, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 88);
	manaValue = std::make_shared<CLabel>(386, 110, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 88);
	labels.push_back(std::make_shared<CLabel>(591, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Morale", 50));
	labels.push_back(std::make_shared<CLabel>(646, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Luck", 50));
	const auto skillsHeading = std::make_shared<CLabel>(16, 176, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "Skills / learned perks", 376);
	labels.push_back(skillsHeading);
	// The visible mark and its 16px click target follow the rendered heading
	// width instead of occupying a fixed, empty-column gap.
	move(growthButton, Point(14 + static_cast<int>(skillsHeading->getWidth()), 176));
	for(size_t i = 0; i < secSkills.size(); ++i)
	{
		const int y = 192 + static_cast<int>(i) * 44;
		move(secSkills[i], Point(12, y));
		move(secSkillValues[i], Point(60, y + 4));
		secSkillNames[i] = heroWindowLabelText::makeSkillName(*this, Point(60, y + 22), 78, true);
		secSkillValues[i]->setMaxWidth(74);
		for(int ability = 0; ability < 3; ++ability)
		{
			const int x = 138 + ability * 98;
			auto area = std::make_shared<LRClickableAreaWText>(Rect(x, y, 98, 44), "");
			area->disable();
			provisionalAbilityAreas.push_back(area);
			provisionalAbilityIcons[i].push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("NH_perk_neutral"), 0, 0, x + 2, y));
			provisionalAbilityIcons[i].back()->disable();
			const std::array cellLabels = {
				std::make_shared<CLabel>(x + 48, y + 14, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 44),
				std::make_shared<CLabel>(x + 48, y + 4, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 46),
				std::make_shared<CLabel>(x + 48, y + 22, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 46)
			};
			for(const auto & label : cellLabels)
			{
				labels.push_back(label);
				provisionalAbilityLabels[i].push_back(label);
			}
		}
	}
	commandArea = std::make_shared<LRClickableAreaWText>(Rect(152, 88, 140, 44), LIBRARY->generaltexth->translate("new-horizons.command.description"));
	movementArea = std::make_shared<LRClickableAreaWText>(Rect(152, 132, 140, 44), "Movement points");
	legacySiegeArea = std::make_shared<LRClickableAreaWText>(Rect(674, 12, 54, 76), "Siege rating available to this hero");
	for(const auto & field : {std::make_pair(Point(152, 132), "Movement")})
	{
		const auto iconKey = "NH_hero_movement_44";
		capabilityIcons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin(iconKey), 0, 0, field.first.x, field.first.y));
		labels.push_back(std::make_shared<CLabel>(field.first.x + 94, field.first.y + 4, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, field.second, 88));
	}
	movementValue = std::make_shared<CLabel>(246, 154, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 88);
	// All three status glyphs are30px, below the42px primary attributes.
	capabilityIcons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("NH_capability_siege_32"),
		0, Rect(686, 36, 30, 30)));
	labels.push_back(std::make_shared<CLabel>(701, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Siege", 50));
	legacySiegeValue = std::make_shared<CLabel>(701, 80, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "", 44);
	addCommandIcon();
	labels.push_back(std::make_shared<CLabel>(246, 92, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("new-horizons.command.name"), 88));
	commandValue = std::make_shared<CLabel>(246, 118, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "", 88);
}

void CHeroWindow::restoreLegacyLayout()
{
	OBJECT_CONSTRUCTION;
	newHorizonsLayout = false;
	if(temporaryEffectsButton)
		temporaryEffectsButton->disable();
	setBackgroundPresentation(ImagePath::builtin(ENGINE->isRoeData() ? "HeroScr3" : "HeroScr4"), PLAYER_COLORED);
	const auto move = [this](const auto & widget, Point point)
	{
		widget->moveTo(pos.topLeft() + point);
	};
	// Reuse the artifact and garrison owners. In particular, do not destroy an
	// artifact holder to resize: its destructor can return a picked artifact.
	if(arts)
		move(arts, Point(-65, -8));
	if(garr)
	{
		move(garr, Point(15, 485));
		for(const auto & split : garr->splitButtons)
			move(split, Point(539, 519));
	}
	banner->enable();
	move(banner, Point(606, 8));
	move(name, Point(190, 38));
	name->setMaxWidth(0);
	move(title, Point(175, 65));
	title->setMaxWidth(180);
	move(growthButton, Point(273, 53));
	move(portraitImage, Point(19, 19));
	move(portraitArea, Point(18, 18));
	move(portraitWikiArea, Point(18, 18));
	move(quitButton, Point(609, 516));
	move(backpackButton, Point(424, 429));
	move(questlogButton, Point(314, 429));
	move(dismissButton, Point(534, 429));
	if(!settings["general"]["enableUiEnhancements"].Bool())
	{
		backpackButton->disable();
		move(dismissButton, Point(454, 429));
		dismissLabel = std::make_shared<CTextBox>(LIBRARY->generaltexth->translate("core.jktext.8"), Rect(370, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
		questlogLabel = std::make_shared<CTextBox>(LIBRARY->generaltexth->translate("core.jktext.9"), Rect(510, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	}
	if(curHero->getCommander() && !commanderButton)
	{
		commanderButton = std::make_shared<CButton>(Point(317, 18), AnimationPath::builtin("heroCommander"), CButton::tooltipLocalized("vcmi.heroWindow.openCommander"), [this](){ commanderWindow(); }, EShortcut::HERO_COMMANDER);
		commanderButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("heroWindow/commanderButtonIcon")));
	}
	// Both entries are created as CToggleButton above; CToggleBase exposes state,
	// not widget positioning. Recover the concrete button before moving it.
	move(std::static_pointer_cast<CToggleButton>(formations->buttons.at(0)), Point(481, 483));
	move(std::static_pointer_cast<CToggleButton>(formations->buttons.at(1)), Point(481, 519));
	for(size_t i = 0; i < heroList.size(); ++i)
		move(heroList[i], Point(612, 87 + static_cast<int>(i) * 54));
	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 559, 660, 19), 7, 559));
	// Constructor-time activation only dispatches the base implementation.
	// Register the completed replacement, but never steal an overlying modal's bar.
	if(isActive())
		statusbar->activate();
	for(size_t i = 0; i < primSkillAreas.size(); ++i)
	{
		const int x = 30 + static_cast<int>(i) * 70;
		move(primSkillAreas[i], Point(x, 109));
		primSkillAreas[i]->pos.w = 42;
		primSkillAreas[i]->pos.h = 64;
		move(primSkillValues[i], Point(x + 23, 166));
		move(primSkillImages[i == 3 ? 5 : i], Point(x + 2, 111));
		move(labels[i], Point(x + 23, 99));
	}
	move(primSkillImages[3], Point(162, 230));
	move(primSkillImages[4], Point(20, 230));
	move(specImage, Point(18, 180));
	move(specArea, Point(18, 180));
	move(expArea, Point(18, 228));
	move(spellPointsArea, Point(162, 228));
	morale->small = false;
	luck->small = false;
	morale->centeredReadout = false;
	luck->centeredReadout = false;
	morale->pos.w = luck->pos.w = 53;
	morale->pos.h = luck->pos.h = 45;
	move(morale, Point(175, 179));
	move(luck, Point(233, 179));
	move(labels[4], Point(69, 183));
	move(labels[5], Point(69, 232));
	move(labels[6], Point(213, 232));
	specName = std::make_shared<CLabel>(69, 205, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 104);
	expValue = std::make_shared<CLabel>(68, 252);
	manaValue = std::make_shared<CLabel>(211, 252);
	for(size_t i = 0; i < 7; ++i)
	{
		labels[i]->alignment = i < 4 ? ETextAlignment::CENTER : ETextAlignment::TOPLEFT;
		labels[i]->setMaxWidth(0);
		labels[i]->setText(LIBRARY->generaltexth->translate("core.jktext." + std::to_string(i + 1)));
	}
	for(size_t i = 7; i < labels.size(); ++i)
		labels[i]->disable();
	for(const auto & value : growthValues)
		value->disable();
	for(const auto & area : provisionalAbilityAreas)
		area->disable();
	for(const auto & icons : provisionalAbilityIcons)
		for(const auto & icon : icons)
			icon->disable();
	for(const auto & icon : capabilityIcons)
		icon->disable();
	movementArea->disable();
	commandArea->disable();
	commandValue->disable();
	legacySiegeArea->disable();
	movementValue->disable();
	legacySiegeValue->disable();
	move(learnedPerksSummary, Point(342, 404));
	learnedPerksSummary->enable();
	move(legacyLeadershipLabel, Point(438, 408));
	move(legacySiegeLabel, Point(534, 408));
	move(legacyLeadershipImage, Point(410, 404));
	move(legacySiegeImage, Point(506, 404));
	move(legacyBoneCollectorImage, Point(314, 404));
	if(curHero->secSkills.size() > 8)
	{
		const int lines = (curHero->secSkills.size() + 1) / 2;
		secSkillSlider = std::make_shared<CSlider>(Point(284, 276), 189, [this](int){ updateArtifacts(); }, 4, lines, 0, Orientation::VERTICAL, CSlider::BROWN);
		secSkillSlider->setPanningStep(48);
		secSkillSlider->setScrollBounds(Rect(-266, 0, secSkillSlider->pos.x - pos.x + secSkillSlider->pos.w, secSkillSlider->pos.h));
	}
	for(size_t i = 0; i < secSkills.size(); ++i)
	{
		const bool right = i % 2 != 0;
		const int y = 276 + 48 * (i / 2);
		move(secSkills[i], Point(right ? 162 : 18, y));
		move(secSkillValues[i], Point(right ? 212 : 68, y + 4));
		secSkillNames[i] = heroWindowLabelText::makeSkillName(*this,
			Point(right ? 212 : 68, y + 24), right && secSkillSlider ? 71 : 87, false);
		secSkillValues[i]->setMaxWidth(right && secSkillSlider ? 71 : 87);
	}
}
