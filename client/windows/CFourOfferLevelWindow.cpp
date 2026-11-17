/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CFourOfferLevelWindow.h"
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
#include "../../lib/mapObjects/CGHeroInstance.h"

CFourOfferLevelWindow::CFourOfferLevelWindow(const CGHeroInstance * heroInstance, PrimarySkill primary,
	const newHorizonsHeroes::FourOfferProgressionOffer & savedOffers, QueryID queryID,
	std::function<int(int)> callback, bool rerollAvailable, std::function<int(size_t)> rerollCallback,
	const std::optional<PrimaryGainSnapshot> & gains)
	: CWindowObject(PLAYER_COLORED | BORDERED, ImagePath::builtin("newHorizonsLevelUpBackground.png")),
	  hero(heroInstance), offers(savedOffers), query(queryID), submit(std::move(callback)),
	  reroll(std::move(rerollCallback)), primaryGains(gains)
{
	OBJECT_CONSTRUCTION;
	GAME->interface()->showingDialog->setBusy();
	createHeader(primary);
	calculateEligibility(rerollAvailable);
	createControls();
	createChoices();
	setRedrawParent(true);
}

void CFourOfferLevelWindow::createHeader(PrimarySkill primary)
{
	OBJECT_CONSTRUCTION;
	MetaString title;
	title.appendTextID("core.genrltxt.444");
	title.replaceTextID(hero->getNameTextID());
	header.push_back(std::make_shared<CLabel>(320, 33, FONT_MEDIUM, ETextAlignment::CENTER,
		Colors::WHITE, title.toString(&GAME->translator()), 340));
	auto portrait = std::make_shared<CHeroArea>(297, 66, hero);
	portrait->addClickCallback(nullptr);
	portrait->addRClickCallback([hero = hero] {
		ENGINE->windows().createAndPushWindow<CRClickPopupInt>(std::make_shared<CHeroWindow>(hero));
	});
	header.push_back(std::move(portrait));
	MetaString level;
	level.appendTextID("core.genrltxt.445");
	level.replaceTextID(hero->getNameTextID());
	level.replaceNumber(primaryGains ? primaryGains->level : hero->level);
	level.replaceTextID(hero->getClassNameTextID());
	header.push_back(std::make_shared<CLabel>(320, 162, FONT_SMALL, ETextAlignment::CENTER,
		Colors::WHITE, level.toString(&GAME->translator()), 340));
	if(primaryGains)
	{
		for(size_t i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
		{
			const int x = 36 + static_cast<int>(i) * 148;
			const int gain = primaryGains->gains[i];
			header.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), i, 0, x, 196));
			header.push_back(std::make_shared<CLabel>(x + 78, 209, FONT_SMALL, ETextAlignment::CENTER,
				Colors::WHITE, GAME->translator().translate("core.priskill", i), 90));
			header.push_back(std::make_shared<CLabel>(x + 78, 235, FONT_MEDIUM, ETextAlignment::CENTER,
				Colors::YELLOW, std::string(gain > 0 ? "+" : "") + std::to_string(gain), 90));
		}
	}
	else
	{
		header.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), primary.getNum(), 0, 246, 196));
		header.push_back(std::make_shared<CLabel>(364, 218, FONT_MEDIUM, ETextAlignment::CENTER,
			Colors::WHITE, GAME->translator().translate("core.priskill", primary.getNum()) + " +1"));
	}
}

void CFourOfferLevelWindow::createControls()
{
	OBJECT_CONSTRUCTION;
	// Keep navigation controls alive while they rebuild their choice children.
	// Destroying a button inside its own callback can invalidate callback dispatch.
	confirm = std::make_shared<CButton>(Point(288, 516), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Confirm", "Confirm the selected advancement."), [this] { confirmSelection(); }, EShortcut::GLOBAL_ACCEPT);
	back = std::make_shared<CButton>(Point(184, 516), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Back", "Return to the same four offers without spending this advancement."), [this] { showOffers(); }, EShortcut::GLOBAL_CANCEL);
	back->setTextOverlay("Back", FONT_SMALL, Colors::YELLOW);
	previousPage = std::make_shared<CButton>(Point(24, 516), AnimationPath::builtin("HSBTNS3"),
		CButton::tooltip("Previous perks", "Inspect the previous page of eligible perks."), [this] { changePage(-1); }, EShortcut::MOVE_LEFT);
	nextPage = std::make_shared<CButton>(Point(66, 516), AnimationPath::builtin("HSBTNS5"),
		CButton::tooltip("More perks", "Inspect the next page of eligible perks."), [this] { changePage(1); }, EShortcut::MOVE_RIGHT);
	for(size_t slot = 0; slot < rerollButtons.size(); ++slot)
	{
		rerollButtons[slot] = std::make_shared<CButton>(Point(slot == 0 ? 130 : 430, 516),
			AnimationPath::builtin("settingsWindow/button80"), CButton::tooltip("Reroll", "Replace this Skill offer once."),
			[this, slot] { requestReroll(slot); });
		rerollButtons[slot]->setTextOverlay(slot == 0 ? "Reroll I" : "Reroll II", FONT_SMALL, Colors::YELLOW);
	}
}

void CFourOfferLevelWindow::calculateEligibility(bool available)
{
	rerollAvailable = available && query >= 0;
	const auto candidates = newHorizonsHeroes::fourOfferCandidates(*hero->cb, *hero);
	perkGroups = newHorizonsHeroes::fourOfferPerkGroups(offers, candidates);
	legalReplies = newHorizonsHeroes::legalFourOfferReplies(offers, hero->getPerkState(), candidates);
	for(size_t slot = 0; slot < offers.skills.size(); ++slot)
		currentRanks[slot] = offers.skills[slot] == SecondarySkill::NONE ? 0 : hero->getSecSkillLevel(offers.skills[slot]);
	for(size_t group = 0; group < offers.perkSkills.size(); ++group)
		currentRanks[2 + group] = offers.perkSkills[group].empty() ? 0 : hero->getPerkSkillRank(offers.perkSkills[group]);
	for(size_t slot = 0; slot < rerollable.size(); ++slot)
		rerollable[slot] = rerollAvailable && slotAvailable(slot)
			&& !newHorizonsHeroes::fourOfferRerollCandidates(offers, candidates, slot).empty();
}

bool CFourOfferLevelWindow::slotAvailable(size_t slot) const
{
	if(slot >= 2)
		return slot < 4 && !perkGroups[slot - 2].empty();
	return std::find(legalReplies.begin(), legalReplies.end(), static_cast<int32_t>(slot)) != legalReplies.end();
}

int32_t CFourOfferLevelWindow::perkReply(size_t group, size_t index) const
{
	return newHorizonsHeroes::fourOfferPerkReply(offers, hero->getPerkState(), group,
		perkGroups[group].at(index).selection.perkId);
}

bool CFourOfferLevelWindow::refreshEligibility()
{
	const auto oldGroups = perkGroups;
	const auto oldReplies = legalReplies;
	const auto oldRerolls = rerollable;
	const auto oldRanks = currentRanks;
	std::optional<std::string> chosenPerk;
	if(openGroup && selectedPerk && *selectedPerk < perkGroups[*openGroup].size())
		chosenPerk = perkGroups[*openGroup][*selectedPerk].selection.perkId;
	calculateEligibility(rerollAvailable);
	// External rank grants can leave the same Basic pool eligible while the
	// held parent's actual rank changes. Its caption must still be refreshed.
	if(perkGroups == oldGroups && legalReplies == oldReplies && rerollable == oldRerolls
		&& currentRanks == oldRanks)
		return false;
	if(selectedSlot && !slotAvailable(*selectedSlot))
		selectedSlot.reset();
	selectedPerk.reset();
	if(openGroup)
	{
		const auto & group = perkGroups[*openGroup];
		if(group.empty())
			openGroup.reset();
		else
		{
			page = std::min(page, (group.size() - 1) / 4);
			if(chosenPerk)
				for(size_t i = 0; i < group.size(); ++i)
					if(group[i].selection.perkId == *chosenPerk)
					{
						selectedPerk = i;
						page = i / 4;
						break;
					}
		}
	}
	createChoices();
	return true;
}

void CFourOfferLevelWindow::updateOffer(const newHorizonsHeroes::FourOfferProgressionOffer & savedOffers,
	QueryID queryID, std::function<int(int)> callback, bool available, std::function<int(size_t)> rerollCallback)
{
	offers = savedOffers;
	query = queryID;
	submit = std::move(callback);
	reroll = std::move(rerollCallback);
	request.reset();
	waiting = false;
	resolved = false;
	openGroup.reset();
	selectedPerk.reset();
	calculateEligibility(available);
	createChoices();
}

bool CFourOfferLevelWindow::matchesQuery(QueryID queryID) const
{
	return query == queryID;
}

bool CFourOfferLevelWindow::matchesHero(const CGHeroInstance * heroInstance) const
{
	return hero == heroInstance;
}

void CFourOfferLevelWindow::requestApplied(uint32_t requestID, bool success)
{
	if(resolved || success || !request || *request != requestID)
		return;
	request.reset();
	waiting = false;
	instruction->setText("The choice was not accepted. Choose again.");
	refreshControls();
}

void CFourOfferLevelWindow::queryResolved(QueryID queryID)
{
	if(query != queryID)
		return;
	resolved = true;
	close();
}

void CFourOfferLevelWindow::close()
{
	// A matching resolution may arrive under a help popup. Never pop its top
	// window; the pending-dialog pump retries once the popup has gone away.
	if(resolved && ENGINE->windows().isTopWindow(this))
		CWindowObject::close();
}
