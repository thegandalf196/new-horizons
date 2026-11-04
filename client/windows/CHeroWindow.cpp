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
#include "SpellPointPresentation.h"
#include "CHeroWindow.h"
#include "HeroSpecialtyPresentation.h"
#include "HeroWindowLabelText.h"
#include "NewHorizonsPerkBrowser.h"
#include "NewHorizonsCapturedPerkIcons.h"
#include "NewHorizonsPerkHelp.h"
#include "NewHorizonsPerkAvailability.h"
#include "HeroSkillOddsWindow.h"
#include "HeroTemporaryEffectsWindow.h"
#include "wiki/WikiWindow.h"

#include "CCreatureWindow.h"
#include "CHeroBackpackWindow.h"
#include "CKingdomInterface.h"
#include "CExchangeWindow.h"
#include "GUIClasses.h"

#include "../CPlayerInterface.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/TextAlignment.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/CComponent.h"
#include "../widgets/CGarrisonInt.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
bool useNewHorizonsHeroLayout(const CGHeroInstance * hero)
{
	// Centered body plus native 14px frame and 8px shadow on each side.
	// Never force a global resolution or hide skills/optional Commander mechanics.
	const auto viewport = ENGINE->screenDimensions();
	const bool hasNewHorizonsView = hero && (hero->getPrimaryGrowthView().has_value()
		|| hero->getLeadershipCapacity().has_value() || hero->getSiegeCapabilities().has_value()
		|| newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules));
	return hasNewHorizonsView && !ENGINE->isRoeData()
		&& settings["general"]["enableUiEnhancements"].Bool()
		&& !hero->getCommander() && hero->secSkills.size() <= 8
		&& viewport.x >= 844 && viewport.y >= 668;
}

std::string shortTierReason(newHorizonsPerkAvailability::Reason reason)
{
	using newHorizonsPerkAvailability::Reason;
	switch(reason)
	{
	case Reason::NONE:
		return "Eligible";
	case Reason::NO_CATALOGUE:
		return "No data";
	case Reason::SKILL_NOT_LEARNED:
		return "Skill needed";
	case Reason::PLANNED_INACTIVE:
		return "Inactive";
	case Reason::INSUFFICIENT_RANK:
		return "Rank needed";
	case Reason::EARLIER_TIER_MISSING:
		return "Earlier tier";
	case Reason::TIER_OCCUPIED:
		return "Tier filled";
	case Reason::PER_SKILL_CAP:
		return "Per-Skill limit";
	}
	return "Unavailable";
}

std::string tierSlotText(const newHorizonsPerkAvailability::TierSlot & slot, std::string_view skillName)
{
	newHorizonsPerkAvailability::Evaluation evaluation{slot.status, slot.reason, slot.requiredRank};
	return "Tier: " + newHorizonsPerkAvailability::tierName(slot.requiredRank)
		+ "\nStatus: " + newHorizonsPerkAvailability::statusName(slot.status)
		+ "\n" + newHorizonsPerkAvailability::explanation(evaluation, skillName);
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
	const bool showsPerks = newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules);
	const bool showsDevelopment = showsGrowth || showsCapabilities || showsMasteries || showsPerks;
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
		if(newHorizonsLayout)
			growthButton->setImage(AnimationPath::builtin("NH_info_button_small"));
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
	if(newHorizonsLayout)
	{
		temporaryEffectsButton = std::make_shared<CButton>(Point(560, 480), AnimationPath::builtin("hsbtns4.def"),
			CButton::tooltip("Temporary effects", "Show this hero's active temporary bonuses, penalties and their expiry."),
			[this]() { ENGINE->windows().createAndPushWindow<HeroTemporaryEffectsWindow>(*curHero); });
		temporaryEffectsButton->setImage(AnimationPath::builtin("heroBackpack"));
		temporaryEffectsButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("NH_hero_temporary_effects_icon")));
	}

	formations = std::make_shared<CToggleGroup>(0);
	formations->addToggle(0, std::make_shared<CToggleButton>(newHorizonsLayout ? Point(484, 544) : Point(481, 483), AnimationPath::builtin("hsbtns6.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.23"), LIBRARY->generaltexth->translate("core.heroscrn.29")), 0, EShortcut::HERO_TIGHT_FORMATION));
	formations->addToggle(1, std::make_shared<CToggleButton>(newHorizonsLayout ? Point(484, 576) : Point(481, 519), AnimationPath::builtin("hsbtns7.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.24"), LIBRARY->generaltexth->translate("core.heroscrn.30")), 0, EShortcut::HERO_LOOSE_FORMATION));
	if(newHorizonsLayout)
	{
		demonicReserveButton = std::make_shared<CButton>(Point(604, 544), AnimationPath::builtin("battleUnitAction"),
			CButton::tooltip("Demonic Reserve", "Select an Inferno army stack to store it, or clear the selection to withdraw reserve troops."),
			[this](){ demonicReserveClicked(); });
		demonicReserveButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("NH_demonicGating_basic_small")));
	}

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
	specName = std::make_shared<CLabel>(69, 205, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", 104);

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
		secSkills.back()->setClickPressedCallback([this, i](CComponentHolder & skillArea, const Point & cursorPosition)
		{
			if(!curHero)
				return;

			const size_t offset = secSkillSlider ? static_cast<size_t>(secSkillSlider->getValue()) * 2 : 0;
			const size_t skillIndex = i + offset;
			if(skillIndex < curHero->secSkills.size())
			{
				const auto & skill = curHero->secSkills[skillIndex].first;
				const std::string skillId = skill.toSkill()->getJsonKey();
				if(newHorizonsPerkHelp::skillDefinition(curHero, skillId))
				{
					ENGINE->windows().createAndPushWindow<NewHorizonsPerkBrowser>(*curHero, skillId);
					return;
				}
			}

			// Preserve the standard learned-skill info dialog for legacy Skills
			// and for heroes without a saved New Horizons perk catalogue.
			if(!skillArea.text.empty())
				static_cast<LRClickableAreaWTextComp &>(skillArea).LRClickableAreaWTextComp::clickPressed(cursorPosition);
		});
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
	legacyLeadershipImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_hero_leadership_24"), 0, Rect(410, 404, 24, 24));
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
	refreshDemonicReserveVisibility();
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
	const auto fullTitle = titleText.toString(&GAME->translator());
	const bool titleElided = heroWindowLabelText::setTitle(*title, fullTitle, newHorizonsLayout);

	const auto specialty = heroSpecialtyPresentation(*curHero);
	specArea->text = specialty.description;
	specArea->details = specialty.details;
	specImage->setAnimationPath(specialty.animation(), specialty.frame());
	specImage->setScale(Point(44, 44));
	specArea->hoverText = specialty.name;
	heroWindowLabelText::setSpecialtyName(*specName, specialty.name, newHorizonsLayout);

	tacticsButton = std::make_shared<CToggleButton>(newHorizonsLayout ? Point(544, 544) : Point(539, 483), AnimationPath::builtin("hsbtns8.def"), std::make_pair(LIBRARY->generaltexth->translate("core.heroscrn.26"), LIBRARY->generaltexth->translate("core.heroscrn.31")), 0, EShortcut::HERO_TOGGLE_TACTICS);
	tacticsButton->addHoverText(EButtonState::HIGHLIGHTED, LIBRARY->generaltexth->translate("core.heroscrn.25"));
	tacticsButton->setSelectedSilent(curHero->tacticFormationEnabled);

	MetaString dismissText;
	dismissText.appendTextID("core.heroscrn.16");
	dismissText.replaceTextID(curHero->getNameTextID());
	dismissText.replaceTextID(curHero->getClassNameTextID());

	dismissButton->addHoverText(EButtonState::NORMAL, dismissText.toString(&GAME->translator()));
	portraitArea->hoverText = curHero->getObjectName().toString(&GAME->translator());
	if(titleElided)
		portraitArea->hoverText += " / " + fullTitle;
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
				growthValues[g]->setText("+" + std::to_string(growth->profile.growth[g] + growth->fixedSpecialtyGrowth[g]));
				primSkillAreas[g]->text += " Saved class growth per level: +" + std::to_string(growth->profile.growth[g])
					+ "; specialty growth: +" + std::to_string(growth->fixedSpecialtyGrowth[g])
					+ ". Before caps and extra Skill rolls. Open Hero development for actual last-level gains.";
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
			const int skillRank = learnedSkill ? curHero->getPerkSkillRank(skillId) : 0;
			for(size_t ability = 0; ability < 3; ++ability)
			{
				const auto areaIndex = g * 3 + ability;
				const auto & area = provisionalAbilityAreas.at(areaIndex);
				const auto & cellLabels = provisionalAbilityLabels[g];
				const auto labelIndex = ability * 3;
				for(size_t label = 0; label < 3; ++label)
				{
					cellLabels.at(labelIndex + label)->setText("");
					cellLabels.at(labelIndex + label)->setEnabled(false);
				}
				area->text.clear();
				area->details.clear();
				area->hoverText.clear();
				area->disable();
				provisionalAbilityIcons[g][ability]->disable();
				if(!learnedSkill || !skillDefinition)
					continue;

				const bool selectionSlots = newHorizonsHeroes::usesR2PerkOffers(perkState.rules)
					&& !newHorizonsHeroes::usesStrictPerkProgression(perkState.rules);
				const auto slot = selectionSlots
					? newHorizonsPerkAvailability::evaluateSelectionSlot(perkState, skillId, skillRank, ability)
					: newHorizonsPerkAvailability::evaluateTier(
						perkState, skillId, skillRank, static_cast<int>(ability + 1));
				cellLabels.at(labelIndex)->setText(slot.selectedPerk
					? slot.selectedPerk->name : shortTierReason(slot.reason));
				cellLabels.at(labelIndex + 1)->setText(selectionSlots && !slot.selectedPerk
					? "Perk slot" : newHorizonsPerkAvailability::tierName(slot.requiredRank));
				cellLabels.at(labelIndex + 2)->setText(newHorizonsPerkAvailability::statusName(slot.status));
				for(size_t label = 0; label < 3; ++label)
					cellLabels.at(labelIndex + label)->setEnabled(true);
				if(slot.selectedPerk)
				{
					const auto & perk = *slot.selectedPerk;
					const auto iconKey = newHorizonsPerkIcon(perk.id, perk.name, perkState.rules);
					provisionalAbilityIcons[g][ability]->setAnimationPath(AnimationPath::builtin(iconKey), 0);
					provisionalAbilityIcons[g][ability]->enable();
					const auto details = newHorizonsPerkHelp::format(curHero, skillId,
						perk.name, newHorizonsPerkAvailability::tierName(slot.requiredRank), perk.description)
						+ "\n\n" + newHorizonsPerkAvailability::explanation(
							{slot.status, slot.reason, slot.requiredRank}, newHorizonsPerkHelp::skillName(curHero, skillId));
					area->text = details;
					area->details = perk.details;
					area->hoverText = details;
				}
				else
				{
					const auto details = (selectionSlots
						? "An unfilled perk slot. Each Skill permits at most three selected perks.\n"
							+ newHorizonsPerkAvailability::explanation(
								{slot.status, slot.reason, slot.requiredRank}, newHorizonsPerkHelp::skillName(curHero, skillId))
						: tierSlotText(slot, newHorizonsPerkHelp::skillName(curHero, skillId)))
						+ "\nClick the Skill to browse its ten perks; browsing never acquires a perk.";
					area->text = details;
					area->details.clear();
					area->hoverText = details;
				}
				area->enable();
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
		std::string skillValue = GAME->translator().translate("core.skilllev", level-1);

		heroWindowLabelText::setSkillName(*secSkillNames[g], newHorizonsPerkHelp::skillLabel(curHero, skill), newHorizonsLayout);
		secSkillValues[g]->setText(skillValue);
		secSkills[g]->setSkillForHero(*curHero, skill, level);
	}

	std::ostringstream expstr;
	expstr << curHero->exp;
	expValue->setText(expstr.str());

	manaValue->setText(spellPointPresentation::readout(curHero->getManaAvailable(),
		curHero->manaLimit(), curHero->getBufferSpellPoints()));

	if(newHorizonsLayout)
	{
		const auto leadership = curHero->getLeadershipCapacity();
		const bool perSlotLeadership = leadership && curHero->getCapabilityRules()["rulesetVersion"].Integer() >= 2;
		const int leadershipPerLevel = perSlotLeadership
			? newHorizonsHeroes::capabilityLeadershipPerLevel(curHero->getCapabilityRules())
			: 0;
		leadershipValue->setText(leadership ? std::to_string(leadership->capacity) : "--");
		leadershipGrowthValue->setText(perSlotLeadership ? "+" + std::to_string(leadershipPerLevel) : "");
		leadershipArea->text = !leadership ? "No saved Leadership rules for this hero. Display icon is illustrative."
			: perSlotLeadership ? "Leadership: " + std::to_string(leadership->capacity)
				+ " total, including artifacts and other modifiers. Growth per hero level: +" + std::to_string(leadershipPerLevel)
				+ ". Each army slot is limited independently: maximum stack size = floor(Leadership / that creature's Leadership Requirement)."
			: "Legacy Leadership preview: " + std::to_string(leadership->used) + " / " + std::to_string(leadership->capacity)
				+ " aggregate creatures. Display icon is illustrative.";
		movementValue->setText(std::to_string(curHero->movementPointsRemaining()) + "/" + std::to_string(curHero->movementPointsLimit()));
		movementArea->text = "Movement points remaining / current limit: " + std::to_string(curHero->movementPointsRemaining())
			+ " / " + std::to_string(curHero->movementPointsLimit()) + ".";
		const auto landMovement = newHorizonsMovement::currentDailyMovementBreakdown(curHero, false);
		const auto seaMovement = newHorizonsMovement::currentDailyMovementBreakdown(curHero, true);
		if(landMovement && seaMovement)
		{
			const auto signedValue = [](const std::int64_t value)
			{
				return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
			};
			const auto appendLimitBreakdown = [&signedValue, this](const char * label,
				const newHorizonsMovement::DailyMovementBreakdown & breakdown)
			{
				movementArea->text += "\n" + std::string(label) + " maximum: " + std::to_string(breakdown.limit)
					+ ". Inputs in order (including Logistics and other active bonuses): 200 base; base adjustment " + signedValue(breakdown.baseAdjustment)
					+ "; " + signedValue(breakdown.percentageToBase) + "% to base; flat " + signedValue(breakdown.flat)
					+ "; " + signedValue(breakdown.percentageToAll) + "% to all.";
				if(breakdown.lowerBound)
					movementArea->text += " Minimum bound: " + std::to_string(*breakdown.lowerBound) + ".";
				if(breakdown.upperBound)
					movementArea->text += " Maximum bound: " + std::to_string(*breakdown.upperBound) + ".";
			};

			appendLimitBreakdown("Land", *landMovement);
			appendLimitBreakdown("Sea", *seaMovement);
			if(curHero->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.navigation"))
				movementArea->text += "\nSea includes Navigation's +25% in its percent-to-base total.";

			const auto turnInfo = curHero->getTurnInfo(0);
			movementArea->text += "\nSteps: 10 orthogonal / 14 diagonal; final tile cost rounds up after modifiers.";
			movementArea->text += "\nTerrain: x1.40 non-native, x1.80 Desert, x1.00 native. Native means the hero or entire army qualifies; one native stack alone does not suffice.";
			if(turnInfo->hasNewHorizonsPathfinding())
				movementArea->text += " Active Pathfinding halves only the terrain surcharge.";
			if(turnInfo->hasNewHorizonsWayfarer())
				movementArea->text += " Active Wayfarer caps the terrain multiplier at x1.25 after Pathfinding.";
			movementArea->text += "\nRoad: x0.67.";
			if(turnInfo->hasNewHorizonsRoadmaster())
				movementArea->text += " Active Roadmaster further multiplies road cost by x0.75.";
			movementArea->text += "\nSpecial travel: Water Walk/Fly steps use x1.50. Creature Speed is not a Movement modifier.";
		}
		else
			movementArea->text += " Display icon is illustrative.";
		const auto siege = curHero->getSiegeCapabilities();
		legacySiegeValue->setText(siege ? std::to_string(siege->siegeRating) : "--");
		legacySiegeArea->text = "Siege rating used by Ballista, Catapult, First Aid Tent and defensive tower formulas. It is not spent.\n";
		if(siege)
			legacySiegeArea->text += (curHero->getCapabilityRules()["rulesetVersion"].Integer() >= 3
				? "War Machines rank: " + std::to_string(siege->warMachinesRank) + ". Open Hero development for saved damage/control details."
				: "Skill ranks: Artillery " + std::to_string(siege->artilleryRank) + ", Ballistics " + std::to_string(siege->ballisticsRank)
					+ ", First Aid " + std::to_string(siege->firstAidRank) + ". Open Hero development for saved damage/control details.");
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
				legacySiegeLabel->setText(curHero->getCapabilityRules()["rulesetVersion"].Integer() >= 3
					? "Siege " + std::to_string(siege->siegeRating) + " (War Machines " + std::to_string(siege->warMachinesRank) + ")"
					: "Siege A" + std::to_string(siege->artilleryRank) + " B" + std::to_string(siege->ballisticsRank)
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

	spellPointsArea->text = spellPointPresentation::tooltip(curHero->getManaAvailable(),
		curHero->manaLimit(), curHero->getBufferSpellPoints());

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

	const bool hasBattlecraftTactics = curHero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.tactics");
	if(curHero->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION) == 0 && !hasBattlecraftTactics)
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
