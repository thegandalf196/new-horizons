/*
 * CLevelWindow.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CLevelWindow.h"
#include "NewHorizonsCapturedPerkIcons.h"
#include "NewHorizonsPerkHelp.h"
#include "CHeroWindow.h"
#include "InfoWindows.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "render/Canvas.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/ConditionalWait.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>


CLevelWindow::CLevelWindow(const CGHeroInstance * hero, PrimarySkill pskill, const std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, std::function<void(ui32)> callback,
	const std::optional<PrimaryGainSnapshot> & gains)
	: CWindowObject(PLAYER_COLORED | BORDERED, ImagePath::builtin("newHorizonsLevelUpBackground.png")),
	skillViewOffset(0)
{
	OBJECT_CONSTRUCTION;

	initLevelUpData(hero, skills, perks, callback, gains);
	createLevelUpControls(pskill);
	setRedrawParent(true);
	redraw();
}

void CLevelWindow::initLevelUpData(const CGHeroInstance * heroInstance,
	const std::vector<SecondarySkill> & availableSkills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & availablePerks,
	const std::function<void(ui32)> & callback, const std::optional<PrimaryGainSnapshot> & gains)
{
	primaryGains = gains;
	GAME->interface()->showingDialog->setBusy();
	selectionSubmitted = false;
	hero = heroInstance;
	cb = callback;
	skills = availableSkills;
	perks = skills.empty() ? availablePerks : std::vector<newHorizonsHeroes::PerkOfferCandidate>{};
	skillViewOffset = 0;
	displayedChoiceOrder.clear();
	choiceOrder.resize(skills.size() + perks.size());
	std::iota(choiceOrder.begin(), choiceOrder.end(), 0);
	std::sort(choiceOrder.begin(), choiceOrder.begin() + skills.size(), [this, heroInstance](size_t left, size_t right) {
		if(heroInstance->getSecSkillLevel(skills[left]) == heroInstance->getSecSkillLevel(skills[right]))
			return newHorizonsPerkHelp::skillLabel(heroInstance, skills[left])
				< newHorizonsPerkHelp::skillLabel(heroInstance, skills[right]);
		return heroInstance->getSecSkillLevel(skills[left]) > heroInstance->getSecSkillLevel(skills[right]);
	});
}

void CLevelWindow::createLevelUpControls(PrimarySkill pskill)
{
	buttonLeft.reset();
	buttonRight.reset();
	portrait.reset();
	ok.reset();
	rerollButtons.clear();
	mainTitle.reset();
	levelTitle.reset();
	choiceInstruction.reset();
	skillIcon.reset();
	skillValue.reset();
	primaryGainWidgets.clear();

	createSkillBox();

	const auto choicePageCount = skills.empty()
		? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
		: (skills.size() + 1) / 2;
	if(choicePageCount > 1)
	{
		buttonLeft = std::make_shared<CButton>(Point(24, 516), AnimationPath::builtin("HSBTNS3"), CButton::tooltip(), [this](){
			const auto pages = skills.empty()
				? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
				: (skills.size() + 1) / 2;
			if(skillViewOffset > 0)
				skillViewOffset--;
			else
				skillViewOffset = static_cast<int>(pages) - 1;
			createSkillBox();
		}, EShortcut::MOVE_LEFT);
		buttonRight = std::make_shared<CButton>(Point(66, 516), AnimationPath::builtin("HSBTNS5"), CButton::tooltip(), [this](){
			const auto pages = skills.empty()
				? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
				: (skills.size() + 1) / 2;
			if(skillViewOffset < static_cast<int>(pages) - 1)
				skillViewOffset++;
			else
				skillViewOffset = 0;
			createSkillBox();
		}, EShortcut::MOVE_RIGHT);
	}

	// Native LVLUPBKG header is centered with its original portrait/banner layout.
	portrait = std::make_shared<CHeroArea>(297, 66, hero);
	portrait->addClickCallback(nullptr);
	portrait->addRClickCallback([hero = hero](){ ENGINE->windows().createAndPushWindow<CRClickPopupInt>(std::make_shared<CHeroWindow>(hero)); });
	ok = std::make_shared<CButton>(Point(288, 516), AnimationPath::builtin("IOKAY"), CButton::tooltip(), std::bind(&CLevelWindow::submitSelection, this), EShortcut::GLOBAL_ACCEPT);
	if(skills.empty() && !perks.empty())
	{
		choiceInstruction = std::make_shared<CLabel>(320, 286, FONT_SMALL, ETextAlignment::CENTER,
			Colors::YELLOW, GAME->translator().translate("new-horizons.progression.perkInstruction"));
	}
	else
	{
		choiceInstruction.reset();
	}

	//%s has gained a level.
	MetaString mainTitleText;
	const bool choosingPerk = skills.empty() && !perks.empty();
	if(choosingPerk)
	{
		auto title = GAME->translator().translate("new-horizons.progression.perkTitle");
		boost::replace_all(title, "%HERO%", GAME->translator().translate(hero->getNameTextID()));
		mainTitleText.appendRawString(title);
	}
	else
	{
		mainTitleText.appendTextID("core.genrltxt.444");
		mainTitleText.replaceTextID(hero->getNameTextID());
	}

	mainTitle = std::make_shared<CLabel>(320, 33, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, mainTitleText.toString(&GAME->translator()), 340);

	//%s is now a level %d %s.
	MetaString levelTitleText;
	levelTitleText.appendTextID("core.genrltxt.445");
	levelTitleText.replaceTextID(hero->getNameTextID());
	levelTitleText.replaceNumber(primaryGains ? primaryGains->level : hero->level);
	levelTitleText.replaceTextID(hero->getClassNameTextID());

	levelTitle = std::make_shared<CLabel>(320, 162, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, levelTitleText.toString(&GAME->translator()), 340);
	if(primaryGains && !choosingPerk)
	{
		for(size_t i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
		{
			const int x = 36 + static_cast<int>(i) * 148;
			const int gained = primaryGains->gains[i];
			// These are the canonical Heroes III primary-attribute frames.  The
			// New Horizons glyphs are useful on the development screen, but do not
			// replace the familiar Attack/Defense/Power/Knowledge level-up icons.
			primaryGainWidgets.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), i, 0, x, 196));
			primaryGainWidgets.push_back(std::make_shared<CLabel>(x + 78, 209, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, GAME->translator().translate("core.priskill", i), 90));
			primaryGainWidgets.push_back(std::make_shared<CLabel>(x + 78, 235, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, std::string(gained > 0 ? "+" : "") + std::to_string(gained), 90));
		}
	}
	else if(!choosingPerk)
	{
		MetaString skillValueText;
		skillValueText.appendTextID("core.priskill", pskill.getNum());
		skillValueText.appendRawString(" +1");
		skillIcon = std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), pskill.getNum(), 0, 246, 196);
		skillValue = std::make_shared<CLabel>(364, 218, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, skillValueText.toString(&GAME->translator()));
	}
}

void CLevelWindow::updateLevelUpData(const CGHeroInstance * heroInstance, PrimarySkill pskill,
	const std::vector<SecondarySkill> & availableSkills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & availablePerks,
	const std::function<void(ui32)> & callback, const std::optional<PrimaryGainSnapshot> & gains)
{
	OBJECT_CONSTRUCTION;

	const auto previousChoice = selectedOfferIndex();
	initLevelUpData(heroInstance, availableSkills, availablePerks, callback, gains);
	createLevelUpControls(pskill);
	restoreOfferSelection(previousChoice);
	setRedrawParent(true);
	redraw();
}

void CLevelWindow::createSkillBox()
{
	OBJECT_CONSTRUCTION;
	const auto previousChoice = selectedOfferIndex();
	rerollButtons.clear();

	box.reset();
	skillChoiceBox.reset();
	perkChoiceBox.reset();
	choiceHeaders.clear();
	perkDescription.reset();
	if(skills.empty() && !perks.empty())
		perkDescription = std::make_shared<CTextBox>("", Rect(44, 454, 552, 52), 0, FONT_SMALL);

	const auto choicePageCount = skills.empty()
		? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
		: (skills.size() + 1) / 2;
	const auto page = choicePageCount ? std::min<size_t>(skillViewOffset, choicePageCount - 1) : 0;
	std::vector<size_t> choicesToShow;
	if(skills.empty())
	{
		for(size_t i = page * 4; i < std::min(perks.size(), page * 4 + 4); ++i)
			choicesToShow.push_back(i);
	}
	else
	{
		for(size_t i = page * 2; i < std::min(skills.size(), page * 2 + 2); ++i)
			choicesToShow.push_back(i);
	}
	if(!choicesToShow.empty())
	{
		// Keep the two categories in their own columns. The canonical offer
		// limits are two skills or up to four perks for the newly acquired rank.
		std::vector<size_t> skillChoices;
		std::vector<size_t> perkChoices;
		for(const auto choice : choicesToShow)
		{
			if(choice < skills.size())
			{
				if(skillChoices.size() < 2)
					skillChoices.push_back(choice);
			}
			else
			{
				perkChoices.push_back(choice);
			}
		}
		displayedChoiceOrder.clear();
		for(size_t row = 0; row < std::max(skillChoices.size(), perkChoices.size()); ++row)
		{
			if(row < skillChoices.size())
				displayedChoiceOrder.push_back(skillChoices[row]);
			if(row < perkChoices.size())
				displayedChoiceOrder.push_back(perkChoices[row]);
		}
		if(!skillChoices.empty() && !perkChoices.empty())
		{
			choiceHeaders.push_back(std::make_shared<CLabel>(166, 268, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Skill choices"));
			choiceHeaders.push_back(std::make_shared<CLabel>(474, 268, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Perk choices"));
		}
		else if(!skillChoices.empty())
		{
			choiceHeaders.push_back(std::make_shared<CLabel>(320, 268, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Choose one secondary skill"));
		}
		else if(!perkChoices.empty())
			choiceHeaders.push_back(std::make_shared<CLabel>(320, 268, FONT_MEDIUM,
				ETextAlignment::CENTER, Colors::YELLOW, perkHeaderText()));

		std::vector<std::shared_ptr<CSelectableComponent>> skillComps;
		std::vector<std::shared_ptr<CSelectableComponent>> perkComps;
		for(const size_t originalIndex : displayedChoiceOrder)
		{
			const bool skillChoice = originalIndex < skills.size();
			auto component = skillChoice ? skillComponent(originalIndex)
				: perkComponent(originalIndex - skills.size());
			component->onChoose = std::bind(&CLevelWindow::submitSelection, this);
			(skillChoice ? skillComps : perkComps).push_back(component);
		}

		if(!skillComps.empty())
		{
			const int x = skillComps.size() == 1 ? (640 - 252) / 2 : 44;
			skillChoiceBox = std::make_shared<CComponentBox>(skillComps,
				Rect(x, 296, skillComps.size() == 1 ? 252 : 552, 206),
				[this](int){
					perkChoiceActive = false;
					if(perkChoiceBox)
						perkChoiceBox->clearSelection();
				}, 48, 10, 12, 2, false);
		}
		if(!perkComps.empty())
		{
			const int rows = static_cast<int>((perkComps.size() + 1) / 2);
			const int x = perkComps.size() == 1 ? (640 - 236) / 2 : 56;
			perkChoiceBox = std::make_shared<CComponentBox>(perkComps,
				Rect(x, 304, perkComps.size() == 1 ? 236 : 528, rows * 80 - 16),
				[this, page](int index){
					perkChoiceActive = true;
					if(skillChoiceBox)
						skillChoiceBox->clearSelection();
					if(index >= 0)
						refreshPerkDescription(page * 4 + static_cast<size_t>(index));
				}, 56, 10, 16, 2, true);
		}
		std::vector<EShortcut> skillShortcuts;
		std::vector<EShortcut> perkShortcuts;
		int shortcutIndex = 0;
		for(size_t row = 0; row < std::max(skillComps.size(), perkComps.size()); ++row)
		{
			if(row < skillComps.size())
				skillShortcuts.push_back(vstd::next(EShortcut::SELECT_INDEX_1, shortcutIndex++));
			if(row < perkComps.size())
				perkShortcuts.push_back(vstd::next(EShortcut::SELECT_INDEX_1, shortcutIndex++));
		}
		if(skillChoiceBox)
			skillChoiceBox->setShortcuts(skillShortcuts);
		if(perkChoiceBox)
			perkChoiceBox->setShortcuts(perkShortcuts);
		box = skillChoiceBox ? skillChoiceBox : perkChoiceBox;
		if(skillChoiceBox)
		{
			perkChoiceActive = false;
			skillChoiceBox->selectFirst();
		}
		else if(perkChoiceBox)
		{
			perkChoiceActive = true;
			perkChoiceBox->selectFirst();
		}
	}

	createRerollControls();
	restoreOfferSelection(previousChoice);
	setRedrawParent(true);
	redraw();
}

void CLevelWindow::setCloseOnSelection(bool value)
{
	closeOnSelection = value;
}

void CLevelWindow::submitSelection()
{
	if(rerollPending || selectionSubmitted)
		return;
	const auto selected = selectedOfferIndex();
	if(!selected && choiceOrder.size() > 1)
		return;
	const size_t choice = selected.value_or(choiceOrder.empty() ? 0 : choiceOrder.front());
	// Publish before dispatch: a repeated click must not send a second reply.
	selectionSubmitted = true;
	refreshRerollControls();
	cb(static_cast<ui32>(choice));
	GAME->interface()->showingDialog->setFree();
	if(closeOnSelection)
		close();
}

void CLevelWindow::close()
{
	if(rerollPending)
		return;

	if(!selectionSubmitted && !choiceOrder.empty())
		return;

	CWindowObject::close();
}
