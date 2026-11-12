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
#include "HeroExperienceBar.h"
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
	primSkillImages[3]->disable();
	move(primSkillImages[4], Point(156, 90));
	move(specImage, Point(14, 88));
	move(specArea, Point(12, 88));
	specArea->pos.w = 136;
	specArea->pos.h = 44;
	move(expArea, Point(154, 88));
	expArea->pos.w = 278;
	expArea->pos.h = 44;
	move(spellPointsArea, Point(152, 136));
	spellPointsArea->pos.w = 140;
	spellPointsArea->pos.h = 38;

	// Three compact native glyphs share the leather band ending at 728.
	// The blue hero-list strip starts at 734 and is never a status cell.
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

	// Specialty
	labels[4]->alignment = ETextAlignment::TOPCENTER;
	move(labels[4], Point(103, 92));
	labels[4]->pos.w = 0;
	labels[4]->pos.h = 0;
	labels[4]->setMaxWidth(88);
	labels[4]->setText(LIBRARY->generaltexth->translate("core.jktext.5"));
	specName = std::make_shared<CLabel>(103, 110, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 88);
	move(specName, Point(103, 110));

	// Experience
	labels[5]->alignment = ETextAlignment::TOPLEFT;
	move(labels[5], Point(204, 92));
	labels[5]->pos.w = 100;
	labels[5]->pos.h = 0;
	labels[5]->setMaxWidth(100);
	labels[5]->setText(LIBRARY->generaltexth->translate("core.jktext.6"));
	expValue = std::make_shared<CLabel>(424, 92, FONT_SMALL, ETextAlignment::TOPRIGHT, Colors::WHITE, "", 120);
	move(expValue, Point(424, 92));
	xpProgressBar = std::make_shared<HeroExperienceBar>(Point(204, 114), Point(220, 12));
	move(xpProgressBar, Point(204, 114));

	// Row 2: Leadership, Mana, Mana Regen
	const std::string_view fieldName = "Leadership";
	const auto iconKey = fieldName == "Leadership" ? "NH_hero_leadership_44" : "NH_hero_movement_44";
	leadershipImage = std::make_shared<CAnimImage>(AnimationPath::builtin(iconKey), 0, Rect(16, 138, 34, 34));
	move(leadershipImage, Point(16, 138));
	capabilityIcons.push_back(leadershipImage);
	commandArea = std::make_shared<LRClickableAreaWText>(Rect(12, 136, 140, 38), LIBRARY->generaltexth->translate("new-horizons.command.description"));
	move(commandArea, Point(12, 136));
	leadershipLabel = std::make_shared<CLabel>(101, 138, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, "Leadership", 76);
	move(leadershipLabel, Point(101, 138));
	commandValue = std::make_shared<CLabel>(101, 154, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 76);
	move(commandValue, Point(101, 154));

	manaOrbImage = std::make_shared<CPicture>(ImagePath::builtin("heroWindow/manaOrb"), 156, 140);
	move(manaOrbImage, Point(156, 140));
	labels[6]->alignment = ETextAlignment::TOPCENTER;
	move(labels[6], Point(241, 138));
	labels[6]->pos.w = 0;
	labels[6]->pos.h = 0;
	labels[6]->setMaxWidth(76);
	labels[6]->setText("Mana");
	manaValue = std::make_shared<CLabel>(241, 154, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 76);
	move(manaValue, Point(241, 154));

	manaRegenImage = std::make_shared<CPicture>(ImagePath::builtin("heroWindow/manaRegen"), 296, 140);
	move(manaRegenImage, Point(296, 140));
	manaRegenArea = std::make_shared<LRClickableAreaWText>(Rect(292, 136, 140, 38), "");
	move(manaRegenArea, Point(292, 136));
	manaRegenLabel = std::make_shared<CLabel>(381, 138, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, "Mana Regen", 76);
	move(manaRegenLabel, Point(381, 138));
	manaRegenValue = std::make_shared<CLabel>(381, 154, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 76);
	move(manaRegenValue, Point(381, 154));

	// Row 3: Movement, Vision
	movementDynamicImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_hero_movement_44"), 0, Rect(20, 178, 34, 34));
	move(movementDynamicImage, Point(20, 178));
	capabilityIcons.push_back(movementDynamicImage);
	movementArea = std::make_shared<LRClickableAreaWText>(Rect(12, 174, 210, 38), "Movement points");
	move(movementArea, Point(12, 174));
	movementDynamicLabel = std::make_shared<CLabel>(138, 178, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, "Movement", 110);
	move(movementDynamicLabel, Point(138, 178));
	movementValue = std::make_shared<CLabel>(138, 194, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 110);
	move(movementValue, Point(138, 194));

	visionImage = std::make_shared<CPicture>(ImagePath::builtin("heroWindow/visionTelescope"), 230, 180);
	move(visionImage, Point(230, 180));
	visionArea = std::make_shared<LRClickableAreaWText>(Rect(222, 174, 210, 38), "Vision radius");
	move(visionArea, Point(222, 174));
	visionLabel = std::make_shared<CLabel>(351, 178, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::YELLOW, "Vision", 110);
	move(visionLabel, Point(351, 178));
	visionValue = std::make_shared<CLabel>(351, 194, FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, "", 110);
	move(visionValue, Point(351, 194));

	// Siege and Morale / Luck
	legacySiegeArea = std::make_shared<LRClickableAreaWText>(Rect(674, 12, 54, 76), "Siege rating available to this hero");
	move(legacySiegeArea, Point(674, 12));
	auto siegeIcon = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_capability_siege_32"),
		0, Rect(686, 36, 30, 30));
	move(siegeIcon, Point(686, 36));
	capabilityIcons.push_back(siegeIcon);
	auto siegeLabel = std::make_shared<CLabel>(701, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Siege", 50);
	move(siegeLabel, Point(701, 21));
	labels.push_back(siegeLabel);
	legacySiegeValue = std::make_shared<CLabel>(701, 80, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "", 44);
	move(legacySiegeValue, Point(701, 80));
	auto moraleLabel = std::make_shared<CLabel>(591, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Morale", 50);
	move(moraleLabel, Point(591, 21));
	labels.push_back(moraleLabel);
	auto luckLabel = std::make_shared<CLabel>(646, 21, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Luck", 50);
	move(luckLabel, Point(646, 21));
	labels.push_back(luckLabel);

	const auto skillsHeading = std::make_shared<CLabel>(16, 219, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "Skills / learned perks", 376);
	move(skillsHeading, Point(16, 219));
	labels.push_back(skillsHeading);
	move(growthButton, Point(14 + static_cast<int>(skillsHeading->getWidth()), 219));
	for(size_t i = 0; i < secSkills.size(); ++i)
	{
		const int y = 236 + static_cast<int>(i) * 38;
		move(secSkills[i], Point(12, y));
		move(secSkillValues[i], Point(60, y + 2));
		secSkillNames[i] = heroWindowLabelText::makeSkillName(*this, Point(60, y + 18), 78, true);
		move(secSkillNames[i], Point(60, y + 18));
		secSkillValues[i]->setMaxWidth(74);
		for(int ability = 0; ability < 3; ++ability)
		{
			const int x = 138 + ability * 98;
			auto area = std::make_shared<LRClickableAreaWText>(Rect(x, y, 98, 36), "");
			move(area, Point(x, y));
			area->disable();
			provisionalAbilityAreas.push_back(area);
			auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_perk_neutral"), 0, 0, x + 2, y + 2);
			move(icon, Point(x + 2, y + 2));
			icon->disable();
			provisionalAbilityIcons[i].push_back(icon);
			const std::array cellLabels = {
				std::make_shared<CLabel>(x + 38, y + 11, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 56),
				std::make_shared<CLabel>(x + 38, y + 2, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 56),
				std::make_shared<CLabel>(x + 38, y + 20, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "", 56)
			};
			for(size_t c = 0; c < cellLabels.size(); ++c)
			{
				const Point offsets[3] = { Point(x + 38, y + 11), Point(x + 38, y + 2), Point(x + 38, y + 20) };
				move(cellLabels[c], offsets[c]);
				labels.push_back(cellLabels[c]);
				provisionalAbilityLabels[i].push_back(cellLabels[c]);
			}
		}
	}
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
	move(specName, Point(69, 205));
	expValue = std::make_shared<CLabel>(68, 252);
	move(expValue, Point(68, 252));
	manaValue = std::make_shared<CLabel>(211, 252);
	move(manaValue, Point(211, 252));
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
	if(movementArea)
		movementArea->disable();
	if(commandArea)
		commandArea->disable();
	if(commandValue)
		commandValue->disable();
	if(leadershipLabel)
		leadershipLabel->disable();
	if(leadershipImage)
		leadershipImage->disable();
	if(manaOrbImage)
		manaOrbImage->disable();
	if(manaRegenImage)
		manaRegenImage->disable();
	if(manaRegenLabel)
		manaRegenLabel->disable();
	if(manaRegenValue)
		manaRegenValue->disable();
	if(manaRegenArea)
		manaRegenArea->disable();
	if(visionImage)
		visionImage->disable();
	if(visionLabel)
		visionLabel->disable();
	if(visionValue)
		visionValue->disable();
	if(visionArea)
		visionArea->disable();
	if(movementDynamicImage)
		movementDynamicImage->disable();
	if(movementDynamicLabel)
		movementDynamicLabel->disable();
	if(xpProgressBar)
		xpProgressBar->disable();
	if(legacySiegeArea)
		legacySiegeArea->disable();
	if(movementValue)
		movementValue->disable();
	if(legacySiegeValue)
		legacySiegeValue->disable();
	primSkillImages[3]->enable();
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
