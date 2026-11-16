/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CPlayerInterface.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/WindowHandler.h"
#include "media/ISoundPlayer.h"
#include "windows/CFourOfferLevelWindow.h"
#include "../lib/callback/CCallback.h"
#include "../lib/entities/hero/NewHorizonsPrimaryGrowth.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/CSoundBase.h"

void CPlayerInterface::heroGotFourOfferLevel(const CGHeroInstance * hero, PrimarySkill primary,
	const newHorizonsHeroes::FourOfferProgressionOffer & offers, QueryID queryID,
	bool optionalRerollAvailable, QueryID replacesQueryID, int reachedLevel)
{
	commandProgressionChanged(hero->id);
	std::optional<CFourOfferLevelWindow::PrimaryGainSnapshot> gains;
	if(const auto growth = hero->getPrimaryGrowthView())
		gains = CFourOfferLevelWindow::PrimaryGainSnapshot{reachedLevel > 0 ? static_cast<ui32>(reachedLevel) : hero->level, growth->lastGains};
	// Keep the received offer and actual gains, not the hero's later level-up.
	auto show = [this, hero, primary, offers, queryID, optionalRerollAvailable, replacesQueryID, gains]()
	{
		if(replacesQueryID < 0)
			ENGINE->sound().playSound(soundBase::heroNewLevel);
		auto choose = [callback = cb, queryID](int selection)
		{
			return queryID < 0 ? -1 : callback->selectionMade(selection, queryID);
		};
		auto reroll = [callback = cb, queryID](size_t slot)
		{
			return queryID < 0 ? -1 : callback->selectionMade(-2 - static_cast<int>(slot), queryID);
		};
		for(const auto & window : ENGINE->windows().findWindows<CFourOfferLevelWindow>())
		{
			const auto oldQuery = replacesQueryID >= 0 ? replacesQueryID : queryID;
			if(!window->matchesQuery(oldQuery) || !window->matchesHero(hero))
				continue;
			window->updateOffer(offers, queryID, choose, optionalRerollAvailable, reroll);
			return;
		}
		if(replacesQueryID >= 0)
			return; // A stale refresh must never recreate a replaced dialog.
		closeActiveLevelUpDialog();
		ENGINE->windows().createAndPushWindow<CFourOfferLevelWindow>(hero, primary, offers, queryID,
			choose, optionalRerollAvailable, reroll, gains);
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
			if(dialog->state != PendingDialog::State::AwaitingQueryResolution)
				return;
			const auto windows = ENGINE->windows().findWindows<CFourOfferLevelWindow>();
			if(std::none_of(windows.begin(), windows.end(), [hero, queryID](const auto & window)
			{
				return window->matchesQuery(queryID) && window->matchesHero(hero) && window->isWaiting();
			}))
				return;
		}
		// A reroll replaces the existing gate; it is not another advancement.
		dialog->queryID = queryID;
		dialog->showCallback = std::move(show);
		if(dialog->state == PendingDialog::State::AwaitingQueryResolution)
			dialog->showCallback();
		return;
	}
	if(findPendingDialog(queryID) != dialogs.end())
		return;
	createAndQueueDialog(PendingDialog::Type::Blocking, std::move(show), queryID);
	tryShowNextPendingDialog();
}
