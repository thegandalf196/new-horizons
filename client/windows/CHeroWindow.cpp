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
#include "NewHorizonsPerkIcons.h"
#include "NewHorizonsPerkHelp.h"
#include "HeroSkillOddsWindow.h"
#include "wiki/WikiWindow.h"

#include "CCreatureWindow.h"
#include "CHeroBackpackWindow.h"
#include "CKingdomInterface.h"
#include "CExchangeWindow.h"

#include "../CPlayerInterface.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/TextAlignment.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/CComponent.h"
#include "../widgets/CGarrisonInt.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"
#include "render/IRenderHandler.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
bool useNewHorizonsHeroLayout(const CGHeroInstance * hero)
{
	// Centered body plus native 14px frame and 8px shadow on each side.
	// Never force a global resolution or hide skills/optional Commander mechanics.
	const auto viewport = ENGINE->screenDimensions();
	return hero && hero->getPrimaryGrowthView().has_value() && !ENGINE->isRoeData()
		&& settings["general"]["enableUiEnhancements"].Bool()
		&& !hero->getCommander() && hero->secSkills.size() <= 8
		&& viewport.x >= 844 && viewport.y >= 668;
}
}

void CHeroSwitcher::clickPressed(const Point & cursorPosition)
{
	//TODO: do not recreate window
	if (false)
	{
		owner->updateArtifacts();
	}
	else
	{
		const CGHeroInstance * buf = hero;
		ENGINE->windows().popWindows(1);
		ENGINE->windows().createAndPushWindow<CHeroWindow>(buf);
	}
}

CHeroSwitcher::CHeroSwitcher(CHeroWindow * owner_, Point pos_, const CGHeroInstance * hero_)
	: CIntObject(LCLICK),
	owner(owner_),
	hero(hero_)
{
	OBJECT_CONSTRUCTION;
	pos += pos_;

	image = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsSmall"), hero->getIconIndex());
	pos.w = image->pos.w;
	pos.h = image->pos.h;
}

CHeroWindow::CHeroWindow(const CGHeroInstance * hero)
	: CWindowObject(useNewHorizonsHeroLayout(hero) ? BORDERED : PLAYER_COLORED,
		ImagePath::builtin(useNewHorizonsHeroLayout(hero) ? "newHorizonsHeroBackground" : ENGINE->isRoeData() ? "HeroScr3" : "HeroScr4"))
{

	OBJECT_CONSTRUCTION;
	curHero = hero;
	newHorizonsLayout = useNewHorizonsHeroLayout(hero);

	banner = std::make_shared<CAnimImage>(AnimationPath::builtin("CREST58"), GAME->interface()->playerID.getNum(), 0, 606, 8);
	name = std::make_shared<CLabel>(190, 38, EFonts::FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW);
	const bool showsGrowth = hero->getPrimaryGrowthView().has_value();
	const bool showsCapabilities = hero->getLeadershipCapacity().has_value() || hero->getSiegeCapabilities().has_value();
	const bool showsMasteries = hero->getMasteryView().has_value();
	const bool showsDevelopment = showsGrowth || showsCapabilities || showsMasteries;
	title = std::make_shared<CLabel>(showsDevelopment ? 175 : 190, 65, EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "", showsDevelopment ? 180 : 0);
	if(showsDevelopment)
	{
		growthButton = std::make_shared<CButton>(Point(273, 53), AnimationPath::builtin("NH_hero_growth_entry"),
			CButton::tooltip("Class skill odds", "View class weights for secondary-skill offers. Actual next-level probabilities depend on eligible choices."),
			[this]
			{
				ENGINE->windows().createAndPushWindow<HeroSkillOddsWindow>(*curHero);
			});
		growthButton->setHoverable(true);
	}

	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 559, 660, 19), 7, 559));

	quitButton = std::make_shared<CButton>(Point(609, 516), AnimationPath::builtin("hsbtns.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.heroscrn.17")), [this](){ close(); }, EShortcut::GLOBAL_RETURN);

	if(settings["general"]["enableUiEnhancements"].Bool())
	{
		questlogButton = std::make_shared<CButton>(Point(314, 429), AnimationPath::builtin("hsbtns4.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.heroscrn.0")), [](){ GAME->interface()->showQuestLog(); }, EShortcut::ADVENTURE_QUEST_LOG);
		backpackButton = std::make_shared<CButton>(Point(424, 429), AnimationPath::builtin("heroBackpack"), CButton::tooltipLocalized("vcmi.heroWindow.openBackpack"), [this](){ createBackpackWindow(); }, EShortcut::HERO_BACKPACK);
		backpackButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("heroWindow/backpackButtonIcon")));
		dismissButton = std::make_shared<CButton>(Point(534, 429), AnimationPath::builtin("hsbtns2.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.heroscrn.28")), [this](){ dismissCurrent(); }, EShortcut::HERO_DISMISS);
	}
	else
	{
		dismissLabel = std::make_shared<CTextBox>(LIBRARY->generaltexth->translate("core.jktext.8"), Rect(370, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
		questlogLabel = std::make_shared<CTextBox>(LIBRARY->generaltexth->translate("core.jktext.9"), Rect(510, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
		dismissButton = std::make_shared<CButton>(Point(454, 429), AnimationPath::builtin("hsbtns2.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.heroscrn.28")), [this](){ dismissCurrent(); }, EShortcut::HERO_DISMISS);
		questlogButton = std::make_shared<CButton>(Point(314, 429), AnimationPath::builtin("hsbtns4.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.heroscrn.0")), [](){ GAME->interface()->showQuestLog(); }, EShortcut::ADVENTURE_QUEST_LOG);
	}
	questlogButton->block(!GAME->interface()->hasJournalEntries());

	formations = std::make_shared<CToggleGroup>(0);
	formations->addToggle(0, std::make_shared<CToggleButton>(newHorizonsLayout ? Point(484, 544) : Point(481, 483), AnimationPath::builtin("hsbtns6.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.23"), LIBRARY->generaltexth->translate("core.heroscrn.29")), 0, EShortcut::HERO_TIGHT_FORMATION));
	formations->addToggle(1, std::make_shared<CToggleButton>(newHorizonsLayout ? Point(484, 576) : Point(481, 519), AnimationPath::builtin("hsbtns7.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.24"), LIBRARY->generaltexth->translate("core.heroscrn.30")), 0, EShortcut::HERO_LOOSE_FORMATION));

	if(hero->getCommander())
	{
		commanderButton = std::make_shared<CButton>(Point(317, 18), AnimationPath::builtin("heroCommander"), CButton::tooltipLocalized("vcmi.heroWindow.openCommander"), [&](){ commanderWindow(); }, EShortcut::HERO_COMMANDER);
		commanderButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("heroWindow/commanderButtonIcon")));
	}

	//right list of heroes
	for(int i=0; i < std::min(GAME->interface()->cb->howManyHeroes(false), 8); i++)
		heroList.push_back(std::make_shared<CHeroSwitcher>(this, Point(newHorizonsLayout ? 739 : 612, (newHorizonsLayout ? 82 : 87) + i * 54), GAME->interface()->cb->getHeroBySerial(i, false)));

	//areas
	portraitArea = std::make_shared<LRClickableAreaWText>(Rect(18, 18, 58, 64));
	portraitImage = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), 0, 0, 19, 19);

	portraitWikiArea = std::make_shared<LRClickableArea>(Rect(18, 18, 58, 64), [this]()
	{
		ENGINE->windows().createAndPushWindow<WikiWindow>(
			WikiWindow::Style::BROWN,
			WikiEntryKey{WikiCategory::HERO, curHero->getHeroType()->getJsonKey()});
	});

	for(int v = 0; v < GameConstants::PRIMARY_SKILLS; ++v)
	{
		auto area = std::make_shared<LRClickableAreaWTextComp>(Rect(30 + 70 * v, 109, 42, 64), ComponentType::PRIM_SKILL);
		area->text = GAME->translator().translate("core.arraytxt", 2+v);
		area->component.subType = PrimarySkill(v);
		MetaString hoverText;
		hoverText.appendTextID("core.heroscrn.1");
		hoverText.replaceTextID("core.priskill", v);
		area->hoverText = hoverText.toString(&GAME->translator());
		primSkillAreas.push_back(area);

		auto value = std::make_shared<CLabel>(53 + 70 * v, 166, FONT_SMALL, ETextAlignment::CENTER);
		primSkillValues.push_back(value);
	}

	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 0, 0, 32, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 1, 0, 102, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 2, 0, 172, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 3, 0, 162, 230));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 4, 0, 20, 230));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 5, 0, 242, 111));

	specImage = std::make_shared<CAnimImage>(AnimationPath::builtin("UN44"), 0, 0, 18, 180);
	specArea = std::make_shared<LRClickableAreaWText>(Rect(18, 180, 136, 42), LIBRARY->generaltexth->translate("core.heroscrn.27"));
	specName = std::make_shared<CLabel>(69, 205);

	expArea = std::make_shared<LRClickableAreaWText>(Rect(18, 228, 136, 42), LIBRARY->generaltexth->translate("core.heroscrn.9"));
	morale = std::make_shared<MoraleLuckBox>(true, Rect(175, 179, 53, 45));
	luck = std::make_shared<MoraleLuckBox>(false, Rect(233, 179, 53, 45));
	spellPointsArea = std::make_shared<LRClickableAreaWText>(Rect(162,228, 136, 42), LIBRARY->generaltexth->translate("core.heroscrn.22"));

	expValue = std::make_shared<CLabel>(68, 252);
	manaValue = std::make_shared<CLabel>(211, 252);

	if(hero->secSkills.size() > 8)
	{
		auto divisionRoundUp = [](int x, int y){ return (x + (y - 1)) / y; };
		int lines = divisionRoundUp(hero->secSkills.size(), 2);
		secSkillSlider = std::make_shared<CSlider>(Point(284, 276), 189, [this](int val){ CHeroWindow::updateArtifacts(); }, 4, lines, 0, Orientation::VERTICAL, CSlider::BROWN);
		secSkillSlider->setPanningStep(48);
		secSkillSlider->setScrollBounds(Rect(-266, 0, secSkillSlider->pos.x - pos.x + secSkillSlider->pos.w, secSkillSlider->pos.h));
	}

	for(int i = 0; i < (newHorizonsLayout ? 8u : std::min<size_t>(hero->secSkills.size(), 8u)); ++i)
	{
		bool isSmallBox = (secSkillSlider && i%2 == 1);
		Rect r(i%2 == 0  ?  18  :  162,  276 + 48 * (i/2), isSmallBox ? 120 : 136,  42);
		secSkills.emplace_back(std::make_shared<CSecSkillPlace>(r.topLeft(), CSecSkillPlace::ImageSize::MEDIUM));

		int x = (i % 2) ? 212 : 68;
		int y = 280 + 48 * (i/2);
		int width = isSmallBox ? 71 : 87;

		secSkillValues.push_back(std::make_shared<CLabel>(x, y, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", width));
		secSkillNames.push_back(std::make_shared<CLabel>(x, y+20, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", width));
	}

	// various texts
	labels.push_back(std::make_shared<CLabel>(52, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.1")));
	labels.push_back(std::make_shared<CLabel>(123, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.2")));
	labels.push_back(std::make_shared<CLabel>(193, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.3")));
	labels.push_back(std::make_shared<CLabel>(262, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.4")));

	labels.push_back(std::make_shared<CLabel>(69, 183, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.5")));
	labels.push_back(std::make_shared<CLabel>(69, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.6")));
	labels.push_back(std::make_shared<CLabel>(213, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("core.jktext.7")));

	learnedPerksSummary = std::make_shared<CTextBox>("", Rect(342, 404, 65, 24), 0,
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	legacyLeadershipLabel = std::make_shared<CLabel>(438, 408, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "Lead --", 65);
	legacySiegeLabel = std::make_shared<CLabel>(534, 408, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "Siege --", 65);
	legacyLeadershipImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_capability_leadership_32"), 0, Rect(410, 404, 24, 24));
	legacySiegeImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_capability_siege_32"), 0, Rect(506, 404, 24, 24));
	legacyBoneCollectorImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_perk_bone_collector"), 0, Rect(314, 404, 24, 24));
	if(newHorizonsLayout)
	{
		configureNewHorizonsLayout();
		learnedPerksSummary->disable();
		legacyLeadershipLabel->disable();
		legacySiegeLabel->disable();
		legacyLeadershipImage->disable();
		legacySiegeImage->disable();
		legacyBoneCollectorImage->disable();
	}
	addUsedEvents(KEYBOARD);
	CHeroWindow::updateArtifacts();
}

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
	move(growthButton, Point(402, 166));
	move(portraitImage, Point(16, 18));
	move(portraitArea, Point(16, 18));
	move(portraitWikiArea, Point(16, 18));
	move(quitButton, Point(740, 570));
	move(backpackButton, Point(444, 480));
	move(questlogButton, Point(502, 480));
	move(dismissButton, Point(560, 480));
	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(0, 608, 800, 16), 0, 608));

	for(size_t i = 0; i < primSkillAreas.size(); ++i)
	{
		const int x = 236 + static_cast<int>(i) * 82;
		move(primSkillAreas[i], Point(x, 12));
		primSkillAreas[i]->pos.w = 82;
		primSkillAreas[i]->pos.h = 76;
		move(primSkillValues[i], Point(x + 40, 80));
		move(primSkillImages[i == 3 ? 5 : i], Point(x + 4, 30));
		move(labels[i], Point(x + 41, 21));
		labels[i]->setMaxWidth(74);
		labels[i]->setText(labels[i]->getText());
		growthValues.push_back(std::make_shared<CLabel>(x + 54, 33, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "", 24));
	}
	move(primSkillImages[3], Point(294, 88));
	move(primSkillImages[4], Point(14, 132));
	move(specImage, Point(14, 88));
	move(specArea, Point(12, 88));
	move(expArea, Point(12, 132));
	move(spellPointsArea, Point(292, 88));
	move(morale, Point(579, 33));
	move(luck, Point(661, 33));
	move(labels[4], Point(62, 92));
	move(labels[5], Point(62, 136));
	move(labels[6], Point(342, 92));
	for(size_t i = 4; i < 7; ++i)
	{
		labels[i]->setMaxWidth(88);
		labels[i]->setText(labels[i]->getText());
	}
	specName = std::make_shared<CLabel>(62, 110, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
	expValue = std::make_shared<CLabel>(62, 154, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
	manaValue = std::make_shared<CLabel>(342, 110, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
	labels.push_back(std::make_shared<CLabel>(568, 13, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "Morale", 74));
	labels.push_back(std::make_shared<CLabel>(650, 13, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "Luck", 74));
	labels.push_back(std::make_shared<CLabel>(16, 176, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "Skills / learned perks", 376));
	for(size_t i = 0; i < secSkills.size(); ++i)
	{
		const int y = 192 + static_cast<int>(i) * 44;
		move(secSkills[i], Point(12, y));
		move(secSkillValues[i], Point(60, y + 4));
		move(secSkillNames[i], Point(60, y + 22));
		secSkillNames[i]->setMaxWidth(74);
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
				std::make_shared<CLabel>(x + 48, y + 14, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 44),
				std::make_shared<CLabel>(x + 48, y + 4, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 46),
				std::make_shared<CLabel>(x + 48, y + 22, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 46)
			};
			for(const auto & label : cellLabels)
			{
				labels.push_back(label);
				provisionalAbilityLabels[i].push_back(label);
			}
		}
	}
	leadershipArea = std::make_shared<LRClickableAreaWText>(Rect(152, 88, 140, 44), "Leadership capacity");
	movementArea = std::make_shared<LRClickableAreaWText>(Rect(152, 132, 140, 44), "Movement points");
	legacySiegeArea = std::make_shared<LRClickableAreaWText>(Rect(292, 132, 140, 44), "Siege points available to this hero");
	for(const auto & field : {std::make_pair(Point(152, 88), "Leadership"), std::make_pair(Point(152, 132), "Movement"), std::make_pair(Point(292, 132), "Siege points")})
	{
		if(std::string(field.second) == "Movement")
			labels.push_back(std::make_shared<CLabel>(field.first.x + 4, field.first.y + 14, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "TBD", 36));
		else
		{
			const auto iconKey = std::string(field.second) == "Leadership" ? "NH_capability_leadership" : "NH_capability_siege";
			capabilityIcons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin(iconKey), 0, 0, field.first.x, field.first.y));
		}
		labels.push_back(std::make_shared<CLabel>(field.first.x + 50, field.first.y + 4, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, field.second, 88));
	}
	leadershipValue = std::make_shared<CLabel>(202, 110, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
	movementValue = std::make_shared<CLabel>(202, 154, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
	legacySiegeValue = std::make_shared<CLabel>(342, 154, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 88);
}

void CHeroWindow::restoreLegacyLayout()
{
	OBJECT_CONSTRUCTION;
	newHorizonsLayout = false;
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
	move(morale, Point(175, 179));
	move(luck, Point(233, 179));
	move(labels[4], Point(69, 183));
	move(labels[5], Point(69, 232));
	move(labels[6], Point(213, 232));
	specName = std::make_shared<CLabel>(69, 205);
	expValue = std::make_shared<CLabel>(68, 252);
	manaValue = std::make_shared<CLabel>(211, 252);
	for(size_t i = 0; i < 7; ++i)
	{
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
	leadershipArea->disable();
	for(const auto & icon : capabilityIcons)
		icon->disable();
	movementArea->disable();
	legacySiegeArea->disable();
	leadershipValue->disable();
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
		move(secSkillNames[i], Point(right ? 212 : 68, y + 24));
		secSkillNames[i]->setMaxWidth(right && secSkillSlider ? 71 : 87);
		secSkillValues[i]->setMaxWidth(right && secSkillSlider ? 71 : 87);
	}
}

void CHeroWindow::onScreenResize()
{
	const bool refresh = newHorizonsLayout;
	if(newHorizonsLayout && !useNewHorizonsHeroLayout(curHero))
		restoreLegacyLayout();
	CWindowObject::onScreenResize();
	if(refresh)
		refreshHero(isActive());
}

void CHeroWindow::keyPressed(EShortcut key)
{
	if(key == EShortcut::ADVENTURE_OPEN_WIKI)
		ENGINE->windows().createAndPushWindow<WikiWindow>(
			WikiWindow::Style::BROWN,
			WikiEntryKey{WikiCategory::HERO, curHero->getHeroType()->getJsonKey()});
}

void CHeroWindow::updateArtifacts()
{
	refreshHero(true);
}

void CHeroWindow::refreshHero(bool refreshArtifactInteraction)
{
	OBJECT_CONSTRUCTION;

	assert(curHero);
	if(newHorizonsLayout && !useNewHorizonsHeroLayout(curHero))
		restoreLegacyLayout();
	// An inactive resize only changes presentation. The inherited refresh also
	// writes the global drag cursor and hover text, which belong to the active modal.
	if(refreshArtifactInteraction)
		CWindowWithArtifacts::updateArtifacts();

	name->setText(GAME->translator().translate(curHero->getNameTextID()));
	MetaString titleText;
	titleText.appendTextID("core.genrltxt.342");
	titleText.replaceNumber(curHero->level);
	titleText.replaceTextID(curHero->getClassNameTextID());
	title->setText(titleText.toString(&GAME->translator()));

	specArea->text = curHero->getHeroType()->getSpecialtyDescriptionTranslated();
	specImage->setFrame(curHero->getHeroType()->imageIndex);
	specName->setText(curHero->getHeroType()->getSpecialtyNameTranslated());

	tacticsButton = std::make_shared<CToggleButton>(newHorizonsLayout ? Point(544, 544) : Point(539, 483), AnimationPath::builtin("hsbtns8.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.26"), LIBRARY->generaltexth->translate("core.heroscrn.31")), 0, EShortcut::HERO_TOGGLE_TACTICS);
	tacticsButton->addHoverText(EButtonState::HIGHLIGHTED, LIBRARY->generaltexth->translate("core.heroscrn.25"));
	tacticsButton->setSelectedSilent(curHero->tacticFormationEnabled);

	MetaString dismissText;
	dismissText.appendTextID("core.heroscrn.16");
	dismissText.replaceTextID(curHero->getNameTextID());
	dismissText.replaceTextID(curHero->getClassNameTextID());

	dismissButton->addHoverText(EButtonState::NORMAL, dismissText.toString(&GAME->translator()));
	portraitArea->hoverText = curHero->getObjectName().toString(&GAME->translator());
	portraitArea->text = GAME->translator().translate(curHero->getBiographyTextID());
	portraitImage->setFrame(curHero->getIconIndex());

	{
		if(!garr)
		{
			bool removableTroops = curHero->getOwner() == GAME->interface()->playerID;
			MetaString helpBoxText = MetaString::createFromTextID("core.heroscrn.32");
			helpBoxText.replaceTextID("core.genrltxt.43");
			std::string helpBox = helpBoxText.toString(&GAME->translator());

			garr = std::make_shared<CGarrisonInt>(newHorizonsLayout ? Point(12, 544) : Point(15, 485), 8, Point(), curHero, nullptr, removableTroops);
			auto split = std::make_shared<CButton>(newHorizonsLayout ? Point(544, 576) : Point(539, 519), AnimationPath::builtin("hsbtns9.def"), CButton::tooltip(LIBRARY->generaltexth->allTexts[256], helpBox), [this](){ garr->splitClick(); }, EShortcut::HERO_ARMY_SPLIT);
			garr->addSplitBtn(split);
		}
		if(!arts)
		{
			arts = std::make_shared<CArtifactsOfHeroMain>(newHorizonsLayout ? Point(65, 66) : Point(-65, -8));
			arts->clickPressedCallback = [this](const CArtPlace & artPlace, const Point & cursorPosition){clickPressedOnArtPlace(curHero, artPlace.slot, true, false, false, cursorPosition);};
			arts->showPopupCallback = [this](CArtPlace & artPlace, const Point & cursorPosition){showArtifactPopup(*arts, artPlace, cursorPosition);};
			arts->gestureCallback = [this](const CArtPlace & artPlace, const Point & cursorPosition){showQuickBackpackWindow(curHero, artPlace.slot, cursorPosition);};
			arts->setHero(curHero);
			addSet(arts);
			enableKeyboardShortcuts();
		}

		int serial = GAME->interface()->cb->getHeroSerial(curHero, false);

		listSelection.reset();
		if(serial >= 0)
			listSelection = std::make_shared<CPicture>(ImagePath::builtin("HPSYYY"), newHorizonsLayout ? 739 : 612, (newHorizonsLayout ? 28 : 33) + serial * 54);
	}

	//primary skills support
	const auto growth = curHero->getPrimaryGrowthView();
	for(size_t g=0; g<primSkillAreas.size(); ++g)
	{
		int value = curHero->getPrimSkillLevel(static_cast<PrimarySkill>(g));
		primSkillAreas[g]->component.value = value;
		primSkillValues[g]->setText(std::to_string(value));
		if(growth)
		{
			const auto attribute = static_cast<PrimarySkill>(g);
			if(attribute == PrimarySkill::ATTACK || attribute == PrimarySkill::DEFENSE)
				primSkillAreas[g]->text = "This hero rating affects command strength. It is not added directly to creature Attack or Defense.";
			else if(attribute == PrimarySkill::SPELL_POWER)
				primSkillAreas[g]->text = "Spell Power uses a scaling divisor of " + std::to_string(growth->powerDivisor) + ". Consult spell descriptions and costs in the spellbook.";
			else if(attribute == PrimarySkill::KNOWLEDGE)
				primSkillAreas[g]->text = "Knowledge supplies base mana directly. Skills and artifacts modify the final mana limit shown on this hero screen.";
			if(newHorizonsLayout)
			{
				growthValues[g]->setText("+" + std::to_string(growth->profile.growth[g]));
				primSkillAreas[g]->text += " Saved class growth per level: +" + std::to_string(growth->profile.growth[g])
					+ ". This is the class proposal before caps, not a prediction of extra skill rolls or actual last-level gains. Open Hero development for details.";
			}
		}
	}

	//secondary skills support
	for(size_t g=0; g < secSkills.size(); ++g)
	{
		int offset = secSkillSlider ? secSkillSlider->getValue() * 2 : 0;
		if(newHorizonsLayout)
		{
			// Keep all reserved cells visible, but bind them to the hero's saved
			// perk selections when a row has a New Horizons skill.  This is a
			// presentation-only readback: selecting a cell never changes state.
			const bool learnedSkill = g + offset < curHero->secSkills.size();
			const auto & perkState = curHero->getPerkState();
			const std::string skillId = learnedSkill
				? curHero->secSkills[g + offset].first.toSkill()->getJsonKey() : std::string();
			const auto skillDefinition = learnedSkill
				? newHorizonsPerkHelp::skillDefinition(curHero, skillId) : std::nullopt;
			std::vector<const newHorizonsHeroes::PerkDefinition *> learnedPerks;
			if(skillDefinition)
				for(const auto & selection : perkState.selected)
					if(selection.skillId == skillId)
						for(const auto & perk : skillDefinition->perks)
							if(perk.id == selection.perkId)
							{
								learnedPerks.push_back(&perk);
								break;
							}
			for(size_t ability = 0; ability < 3; ++ability)
			{
				const auto areaIndex = g * 3 + ability;
				const auto & area = provisionalAbilityAreas.at(areaIndex);
				const auto & cellLabels = provisionalAbilityLabels[g];
				const auto labelIndex = ability * 3;
				const bool hasPerk = ability < learnedPerks.size();
				const auto iconKey = newHorizonsPerkIcon(hasPerk ? learnedPerks[ability]->id : std::string());
				provisionalAbilityIcons[g][ability]->setAnimationPath(AnimationPath::builtin(iconKey), 0);
				if(learnedSkill && hasPerk)
					provisionalAbilityIcons[g][ability]->enable();
				else
					provisionalAbilityIcons[g][ability]->disable();
				for(size_t label = 0; label < 3; ++label)
				{
					cellLabels.at(labelIndex + label)->setText("");
					cellLabels.at(labelIndex + label)->setEnabled(learnedSkill && hasPerk);
				}
				area->text.clear();
				area->hoverText.clear();
				area->disable();
				if(!learnedSkill || !hasPerk)
					continue;
				area->enable();

				if(hasPerk)
				{
					const auto * perk = learnedPerks[ability];
					const auto description = newHorizonsPerkHelp::format(curHero, skillId,
						perk->name, newHorizonsPerkHelp::tierName(perk->requiredRank), perk->description);
					area->text = description;
					area->hoverText = description;
					cellLabels.at(labelIndex)->setText("");
					cellLabels.at(labelIndex + 1)->setText(perk->name);
					cellLabels.at(labelIndex + 2)->setText("Learned");
				}
			}
		}
		if(curHero->secSkills.size() < g + offset + 1)
		{
			secSkillNames[g]->setText("");
			secSkillValues[g]->setText("");
			secSkills[g]->setSkill(SecondarySkill::NONE);
			continue;
		}
		SecondarySkill skill = curHero->secSkills[g + offset].first;
		int	level = curHero->getSecSkillLevel(skill);
		std::string skillName = skill.toEntity(LIBRARY)->getNameTranslated();
		std::string skillValue = GAME->translator().translate("core.skilllev", level-1);

		secSkillNames[g]->setText(skillName);
		secSkillValues[g]->setText(skillValue);
		secSkills[g]->setSkill(skill, level);
	}

	std::ostringstream expstr;
	expstr << curHero->exp;
	expValue->setText(expstr.str());

	std::ostringstream manastr;
	manastr << curHero->mana << '/' << curHero->manaLimit();
	manaValue->setText(manastr.str());

	if(newHorizonsLayout)
	{
		const auto leadership = curHero->getLeadershipCapacity();
		const bool perSlotLeadership = leadership && curHero->getCapabilityRules()["rulesetVersion"].Integer() >= 2;
		leadershipValue->setText(leadership ? std::to_string(leadership->capacity) : "--");
		leadershipArea->text = !leadership ? "No saved Leadership rules for this hero. Display icon is illustrative."
			: perSlotLeadership ? "Leadership: " + std::to_string(leadership->capacity)
				+ ". Each army slot is limited independently: maximum stack size = floor(Leadership / that creature's Leadership Requirement)."
			: "Legacy Leadership preview: " + std::to_string(leadership->used) + " / " + std::to_string(leadership->capacity)
				+ " aggregate creatures. Display icon is illustrative.";
		movementValue->setText(std::to_string(curHero->movementPointsRemaining()) + "/" + std::to_string(curHero->movementPointsLimit()));
		movementArea->text = "Movement points remaining / current limit: " + std::to_string(curHero->movementPointsRemaining())
			+ " / " + std::to_string(curHero->movementPointsLimit()) + ". Display icon is illustrative.";
		const auto siege = curHero->getSiegeCapabilities();
		legacySiegeValue->setText(siege ? "A" + std::to_string(siege->artilleryRank) + " B" + std::to_string(siege->ballisticsRank) + " F" + std::to_string(siege->firstAidRank) : "--");
		legacySiegeArea->text = "Existing saved siege capabilities, not a spendable Siege balance. No cost, maximum or refill is implied. Display icon is illustrative.\n";
		if(siege)
			legacySiegeArea->text += "Skill ranks: Artillery " + std::to_string(siege->artilleryRank) + ", Ballistics " + std::to_string(siege->ballisticsRank)
				+ ", First Aid " + std::to_string(siege->firstAidRank) + ". Open Hero development for saved damage/control details.";
		else
			legacySiegeArea->text += "No saved siege capability rules for this hero.";
	}

	// Legacy-sized hero windows do not have the New Horizons ability grid.
	// Keep learned perks visible there as a compact, read-only fallback so a
	// perk never appears to vanish merely because the presentation changed.
	if(learnedPerksSummary)
	{
		if(newHorizonsLayout)
		{
			learnedPerksSummary->disable();
		}
		else
		{
			const auto & perkState = curHero->getPerkState();
			const bool hasPerkRules = newHorizonsHeroes::usesPerkRules(perkState.rules);
			std::string summary;
			bool hasBoneCollector = false;
			if(hasPerkRules)
			{
				for(const auto & selection : perkState.selected)
				{
					const auto skill = newHorizonsPerkHelp::skillDefinition(curHero, selection.skillId);
					const auto perk = newHorizonsPerkHelp::perkDefinition(curHero, selection.skillId, selection.perkId);
					if(skill && perk)
					{
						if(!summary.empty())
							summary += ", ";
						summary += perk->name;
						hasBoneCollector = hasBoneCollector || perk->id == "new-horizons:necromancy.boneCollector";
					}
				}
			}
			if(const auto leadership = curHero->getLeadershipCapacity())
			{
				legacyLeadershipLabel->setText("Leadership " + std::to_string(leadership->capacity));
				legacyLeadershipLabel->enable();
				legacyLeadershipImage->enable();
			}
			else
			{
				legacyLeadershipLabel->disable();
				legacyLeadershipImage->disable();
			}
			if(const auto siege = curHero->getSiegeCapabilities())
			{
				legacySiegeLabel->setText("Siege A" + std::to_string(siege->artilleryRank) + " B" + std::to_string(siege->ballisticsRank)
					+ " F" + std::to_string(siege->firstAidRank));
				legacySiegeLabel->enable();
				legacySiegeImage->enable();
			}
			else
			{
				legacySiegeLabel->disable();
				legacySiegeImage->disable();
			}
			if(summary.empty())
			{
				learnedPerksSummary->setText("");
				learnedPerksSummary->disable();
				legacyBoneCollectorImage->disable();
			}
			else
			{
				learnedPerksSummary->setText(summary);
				learnedPerksSummary->enable();
				if(hasBoneCollector)
					legacyBoneCollectorImage->enable();
				else
					legacyBoneCollectorImage->disable();
			}
		}
	}

	MetaString expText;
	expText.appendTextID("core.genrltxt.2");
	expText.replaceNumber(curHero->level);
	expText.replaceNumber(LIBRARY->heroh->reqExp(curHero->level + 1));
	expText.replaceNumber(curHero->exp);
	expArea->text = expText.toString(&GAME->translator());

	MetaString spellPointsText;
	spellPointsText.appendTextID("core.genrltxt.205");
	spellPointsText.replaceTextID(curHero->getNameTextID());
	spellPointsText.replaceNumber(curHero->mana);
	spellPointsText.replaceNumber(curHero->manaLimit());
	spellPointsArea->text = spellPointsText.toString(&GAME->translator());

	//if we have exchange window with this curHero open
	bool noDismiss=false;

	for(auto cew : ENGINE->windows().findWindows<CExchangeWindow>())
	{
		if (cew->holdsGarrison(curHero))
			noDismiss = true;
	}

	//if player only have one hero and no towns
	if(!GAME->interface()->cb->howManyTowns() && GAME->interface()->cb->howManyHeroes() == 1)
		noDismiss = true;

	if(curHero->isMissionCritical())
		noDismiss = true;

	dismissButton->block(noDismiss);

	if(curHero->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION) == 0)
	{
		tacticsButton->block(true);
	}
	else
	{
		tacticsButton->block(false);
		tacticsButton->addCallback([this](bool on){ GAME->interface()->cb->setTactics(curHero, on); });
	}

	formations->resetCallback();
	//setting formations
	formations->setSelected(curHero->formation == EArmyFormation::TIGHT ? 1 : 0);
	formations->addCallback([this](int value){ GAME->interface()->cb->setFormation(curHero, static_cast<EArmyFormation>(value));});

	morale->set(curHero);
	luck->set(curHero);

	redraw();
}

void CHeroWindow::dismissCurrent()
{
	GAME->interface()->showYesNoDialog(LIBRARY->generaltexth->allTexts[22], [this]()
		{
			arts->putBackPickedArtifact();
			close();
			GAME->interface()->cb->dismissHero(curHero);
			arts->setHero(nullptr);
		}, nullptr);
}

void CHeroWindow::createBackpackWindow()
{
	ENGINE->windows().createAndPushWindow<CHeroBackpackWindow>(curHero, artSets);
}

void CHeroWindow::commanderWindow()
{
	const auto pickedArtInst = getPickedArtifact();
	const auto hero = getHeroPickedArtifact();

	if(pickedArtInst)
	{
		const auto freeSlot = ArtifactUtils::getArtAnyPosition(curHero->getCommander(), pickedArtInst->getTypeId());
		if(vstd::contains(ArtifactUtils::commanderSlots(), freeSlot)) // We don't want to put it in commander's backpack!
		{
			ArtifactLocation dst(curHero->id, freeSlot);
			dst.creature = SlotID::COMMANDER_SLOT_PLACEHOLDER;
			GAME->interface()->cb->swapArtifacts(ArtifactLocation(hero->id, ArtifactPosition::TRANSITION_POS), dst);
		}
	}
	else
	{
		ENGINE->windows().createAndPushWindow<CStackWindow>(curHero->getCommander(), false);
	}
}

void CHeroWindow::updateGarrisons()
{
	garr->recreateSlots();
	if(newHorizonsLayout)
		updateArtifacts(); // Refresh live capacity/movement too; retain the existing artifact holder.
	else
		morale->set(curHero);
}

bool CHeroWindow::holdsGarrison(const CArmedInstance * army)
{
	return army == curHero;
}
