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

namespace
{
// A visible parent identity, independent of the perk icon and selection callback.
class CLevelUpPerkComponent : public CSelectableComponent
{
	std::shared_ptr<CComponent> parentSkill;

public:
	CLevelUpPerkComponent(SecondarySkill skill, bool hasSkill, int requiredRank,
		const std::string & name, const AnimationPath & icon, const std::string & parentName)
		: CSelectableComponent(hasSkill ? ComponentType::SEC_SKILL : ComponentType::NONE,
			hasSkill ? skill : SecondarySkill(0), name, CComponent::medium)
	{
		OBJECT_CONSTRUCTION;
		setCustomIcon(icon);
		setHorizontalLayout(244, 46, true);
		pos.h = 84;
		parentSkill = std::make_shared<CComponent>(hasSkill ? ComponentType::SEC_SKILL : ComponentType::NONE,
			hasSkill ? skill : SecondarySkill(0), requiredRank, CComponent::small);
		parentSkill->customSubtitle = parentName + "\n"
			+ GAME->translator().translate("core.skilllev", requiredRank - 1);
		parentSkill->setHorizontalLayout(228, 34, true);
		parentSkill->moveTo(pos.topLeft() + Point(8, 48));
	}
};
}

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
	perks = availablePerks;
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
		: std::max((skills.size() + 1) / 2, (perks.size() + 1) / 2);
	if(choicePageCount > 1)
	{
		buttonLeft = std::make_shared<CButton>(Point(24, 532), AnimationPath::builtin("HSBTNS3"), CButton::tooltip(), [this](){
			const auto pages = skills.empty()
				? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
				: std::max((skills.size() + 1) / 2, (perks.size() + 1) / 2);
			if(skillViewOffset > 0)
				skillViewOffset--;
			else
				skillViewOffset = static_cast<int>(pages) - 1;
			createSkillBox();
		}, EShortcut::MOVE_LEFT);
		buttonRight = std::make_shared<CButton>(Point(66, 532), AnimationPath::builtin("HSBTNS5"), CButton::tooltip(), [this](){
			const auto pages = skills.empty()
				? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
				: std::max((skills.size() + 1) / 2, (perks.size() + 1) / 2);
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
	ok = std::make_shared<CButton>(Point(288, 532), AnimationPath::builtin("IOKAY"), CButton::tooltip(), std::bind(&CLevelWindow::submitSelection, this), EShortcut::GLOBAL_ACCEPT);
	const std::string instructionText = (skills.empty() && !perks.empty())
		? "Choose one perk"
		: (!skills.empty() && perks.empty() ? "Choose one secondary skill" : "Choose one skill or perk");
	choiceInstruction = std::make_shared<CLabel>(320, 516, FONT_SMALL, ETextAlignment::CENTER,
		Colors::WHITE, instructionText);

	//%s has gained a level.
	MetaString mainTitleText;
	mainTitleText.appendTextID("core.genrltxt.444");
	mainTitleText.replaceTextID(hero->getNameTextID());

	mainTitle = std::make_shared<CLabel>(320, 33, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, mainTitleText.toString(&GAME->translator()), 340);

	//%s is now a level %d %s.
	MetaString levelTitleText;
	levelTitleText.appendTextID("core.genrltxt.445");
	levelTitleText.replaceTextID(hero->getNameTextID());
	levelTitleText.replaceNumber(primaryGains ? primaryGains->level : hero->level);
	levelTitleText.replaceTextID(hero->getClassNameTextID());

	levelTitle = std::make_shared<CLabel>(320, 162, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, levelTitleText.toString(&GAME->translator()), 340);
	if(primaryGains)
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
	else
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

	const auto choicePageCount = skills.empty()
		? (perks.size() > 4 ? (perks.size() + 3) / 4 : 1)
		: std::max((skills.size() + 1) / 2, (perks.size() + 1) / 2);
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
		for(size_t i = page * 2; i < std::min(perks.size(), page * 2 + 2); ++i)
			choicesToShow.push_back(skills.size() + i);
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
			choiceHeaders.push_back(std::make_shared<CLabel>(166, 278, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Skill choices"));
			choiceHeaders.push_back(std::make_shared<CLabel>(474, 278, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Perk choices"));
		}
		else if(!skillChoices.empty())
		{
			choiceHeaders.push_back(std::make_shared<CLabel>(320, 278, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "Skill choices"));
		}
		else if(!perkChoices.empty())
		{
			const auto & firstPerk = perks.front();
			const int decoded = SecondarySkill::decode(firstPerk.selection.skillId == "new-horizons:necromancy"
				? "core:necromancy" : firstPerk.selection.skillId);
			const SecondarySkill iconSkill = decoded >= 0 && SecondarySkill::encode(decoded) == firstPerk.selection.skillId
				? SecondarySkill(decoded)
				: (firstPerk.selection.skillId == "new-horizons:necromancy"
					? SecondarySkill(SecondarySkill::NECROMANCY) : SecondarySkill(SecondarySkill::NONE));
			const auto parentName = iconSkill != SecondarySkill::NONE
				? newHorizonsPerkHelp::skillLabel(hero, iconSkill) : firstPerk.selection.skillId;
			const auto headerText = newHorizonsPerkHelp::tierName(firstPerk.requiredRank) + " " + parentName + " perk choices";
			choiceHeaders.push_back(std::make_shared<CLabel>(320, 278, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, headerText));
		}

		std::vector<std::shared_ptr<CSelectableComponent>> skillComps;
		std::vector<std::shared_ptr<CSelectableComponent>> perkComps;
		for(const size_t originalIndex : displayedChoiceOrder)
		{
			std::shared_ptr<CSelectableComponent> comp;
			if(originalIndex < skills.size())
			{
				const auto skill = skills[originalIndex];
				const int currentRank = hero->getSecSkillLevel(skill);
				const int offeredRank = currentRank + 1;
				comp = std::make_shared<CSelectableComponent>(ComponentType::SEC_SKILL, skill,
					offeredRank, CComponent::medium);
				comp->customDescription = newHorizonsPerkHelp::skillDescription(hero, skill, offeredRank);
				const auto offeredRankText = GAME->translator().translate("core.skilllev", offeredRank - 1);
				comp->customSubtitle = offeredRankText + "\n"
					+ newHorizonsPerkHelp::skillLabel(hero, skill);
				skillComps.push_back(comp);
				comp->setHorizontalLayout(rerollAvailable ? 156 : 244, 84, true);
			}
			else
			{
				const auto & perk = perks.at(originalIndex - skills.size());
				const int decoded = SecondarySkill::decode(perk.selection.skillId == "new-horizons:necromancy"
					? "core:necromancy" : perk.selection.skillId);
				const SecondarySkill iconSkill = decoded >= 0 && SecondarySkill::encode(decoded) == perk.selection.skillId
					? SecondarySkill(decoded)
					: (perk.selection.skillId == "new-horizons:necromancy"
						? SecondarySkill(SecondarySkill::NECROMANCY) : SecondarySkill(SecondarySkill::NONE));
				const bool hasCanonicalSkillIcon = (decoded >= 0 && SecondarySkill::encode(decoded) == perk.selection.skillId)
					|| perk.selection.skillId == "new-horizons:necromancy";
				const auto parentName = hasCanonicalSkillIcon
					? newHorizonsPerkHelp::skillLabel(hero, iconSkill) : perk.selection.skillId;
				comp = std::make_shared<CLevelUpPerkComponent>(iconSkill, hasCanonicalSkillIcon,
					perk.requiredRank, perk.name, AnimationPath::builtin(newHorizonsPerkIcon(perk.selection.perkId, perk.name, hero->getPerkState().rules)), parentName);
				comp->customDescription = newHorizonsPerkHelp::format(hero, perk.selection.skillId,
					perk.name, newHorizonsPerkHelp::tierName(perk.requiredRank), perk.description);
				perkComps.push_back(comp);
			}
			comp->onChoose = std::bind(&CLevelWindow::submitSelection, this);
		}

		// Use one single-column box per category so an absent skill or perk
		// never pulls the other category into its column. The offer contract
		// allows two entries per category. Size each box to its actual rows so
		// a single offer stays aligned with the first recess rather than centered.
		if(!skillComps.empty())
		{
			const int x = perkComps.empty() ? (640 - (rerollAvailable ? 156 : 244)) / 2 : 44;
			skillChoiceBox = std::make_shared<CComponentBox>(skillComps,
				Rect(x, 300, rerollAvailable ? 156 : 244, static_cast<int>(skillComps.size()) * 96 - 12),
				[this](int){
					perkChoiceActive = false;
					if(perkChoiceBox)
						perkChoiceBox->clearSelection();
				}, 12, 4, 12, 1);
		}
		if(!perkComps.empty())
		{
			if(skillComps.empty() && perkComps.size() > 2)
			{
				const int rows = static_cast<int>((perkComps.size() + 1) / 2);
				perkChoiceBox = std::make_shared<CComponentBox>(perkComps,
					Rect(44, 300, 552, rows * 96 - 12),
					[this](int){
						perkChoiceActive = true;
						if(skillChoiceBox)
							skillChoiceBox->clearSelection();
					}, 12, 4, 12, 2);
			}
			else
			{
				const int x = skillComps.empty() ? (640 - 244) / 2 : 352;
				perkChoiceBox = std::make_shared<CComponentBox>(perkComps,
					Rect(x, 300, 244, static_cast<int>(perkComps.size()) * 96 - 12),
					[this](int){
						perkChoiceActive = true;
						if(skillChoiceBox)
							skillChoiceBox->clearSelection();
					}, 12, 4, 12, 1);
			}
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
	if(rerollPending)
		return;

	if(!selectionSubmitted)
	{
		int idx = -1;

		const auto activeBox = perkChoiceActive ? perkChoiceBox : skillChoiceBox;
		if(activeBox)
			idx = activeBox->selectedIndex();

		// If there are skills available, we must not close without producing a valid choice
		// For a single available option, auto-pick it
		if(choiceOrder.empty())
		{
			cb(0);
		}
		else
		{
			if(idx == -1)
			{
				if(choiceOrder.size() == 1)
					idx = 0;
				else
					return; // require explicit selection
			}

			if(displayedChoiceOrder.empty())
				return;
			size_t categoryIndex = 0;
			for(const auto choice : displayedChoiceOrder)
		{
			const bool isPerk = choice >= skills.size();
			if(isPerk == perkChoiceActive)
			{
				if(categoryIndex++ == static_cast<size_t>(idx))
				{
					cb(choice);
					break;
				}
			}
		}
		}

		selectionSubmitted = true;
		refreshRerollControls();
		GAME->interface()->showingDialog->setFree();
	}

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
