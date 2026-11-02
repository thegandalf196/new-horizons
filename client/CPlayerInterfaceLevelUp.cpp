/*
 * CPlayerInterfaceLevelUp.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CPlayerInterface.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/WindowHandler.h"
#include "media/ISoundPlayer.h"
#include "windows/GUIClasses.h"
#include "../lib/callback/CCallback.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/entities/hero/NewHorizonsPrimaryGrowth.h"
#include "../lib/CSoundBase.h"

void CPlayerInterface::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill,
	std::vector<SecondarySkill> & skills, const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks,
	QueryID queryID)
{
	heroGotLevelWithReroll(hero, pskill, skills, perks, queryID, false, QueryID::NONE, 0);
}

void CPlayerInterface::heroGotLevelWithReroll(const CGHeroInstance *hero, PrimarySkill pskill, std::vector<SecondarySkill>& skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, QueryID queryID, bool optionalRerollAvailable, QueryID replacesQueryID, int reachedLevel)
{
	auto availableSkills = skills;
	auto availablePerks = perks;
	std::optional<CLevelWindow::PrimaryGainSnapshot> primaryGains;
	if(const auto growth = hero->getPrimaryGrowthView())
		primaryGains = CLevelWindow::PrimaryGainSnapshot{hero->level, growth->lastGains};

	if(primaryGains && reachedLevel > 0)
		primaryGains->level = reachedLevel;

	// Capture the already-applied level packet's values before queuing the UI;
	// a later level-up must not replace this dialog's actual gain readback.
	auto showLevelUpDialog = [this, hero, pskill, availableSkills = std::move(availableSkills),
		availablePerks = std::move(availablePerks), queryID, primaryGains, optionalRerollAvailable, replacesQueryID]() mutable
	{
		if(replacesQueryID < 0)
			ENGINE->sound().playSound(soundBase::heroNewLevel);
		auto reroll = [this, queryID] { return cb->selectionMade(-1, queryID); };
		auto callback = [this, queryID](ui32 selection)
		{
			if(queryID < 0)
				return;

			cb->selectionMade(selection, queryID);
		};

		for(const auto & levelWindow : ENGINE->windows().findWindows<CLevelWindow>())
		{
			if(replacesQueryID >= 0 && (!levelWindow->matchesQuery(replacesQueryID)
				|| (queryID == replacesQueryID && !levelWindow->matchesHero(hero))))
				continue;
			if(replacesQueryID < 0 && !ENGINE->windows().isTopWindow(levelWindow.get()))
				continue;
			levelWindow->updateLevelUpData(hero, pskill, availableSkills, availablePerks, callback, primaryGains);
			levelWindow->configureReroll(queryID, optionalRerollAvailable, reroll);
			return;
		}
		if(replacesQueryID >= 0)
			return; // Never recreate a stale refresh over another query's dialog.

		closeActiveLevelUpDialog();

		auto levelWindow = std::make_shared<CLevelWindow>(hero, pskill, availableSkills, availablePerks, callback, primaryGains);

		// Free the visible-dialog gate as soon as the player makes a choice.
		// The query-backed dialog queue still keeps manual input blocked until the
		// server resolves this level-up step and advances the chain.
		levelWindow->setCloseOnSelection(queryID < 0);
		levelWindow->configureReroll(queryID, optionalRerollAvailable, reroll);
		ENGINE->windows().pushWindow(levelWindow);
	};

	if(replacesQueryID >= 0)
	{
		auto dialog = findPendingDialog(replacesQueryID);
		const auto destination = findPendingDialog(queryID);
		if(dialog == dialogs.end() || !dialog->isLevelUpDialog()
			|| (destination != dialogs.end() && destination != dialog))
			return;
		if(queryID == replacesQueryID)
		{
			// A confirmation answers a visible reroll request, never a queued offer.
			if(dialog->state != PendingDialog::State::AwaitingQueryResolution)
				return;
			const auto windows = ENGINE->windows().findWindows<CLevelWindow>();
			if(std::none_of(windows.begin(), windows.end(), [hero, queryID](const auto & window)
			{
				return window->matchesQuery(queryID) && window->matchesHero(hero);
			}))
				return;
		}
		// Replace the existing gate atomically. No QueryResolved is sent for a
		// reroll, and appending would queue the new offer behind this busy one.
		dialog->queryID = queryID;
		dialog->showCallback = std::move(showLevelUpDialog);
		if(dialog->state == PendingDialog::State::AwaitingQueryResolution)
			dialog->showCallback();
		return;
	}
	createAndQueueDialog(PendingDialog::Type::Blocking, std::move(showLevelUpDialog), queryID);
	tryShowNextPendingDialog();
}
