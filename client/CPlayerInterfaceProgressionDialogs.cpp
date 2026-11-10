/*
 * CPlayerInterfaceProgressionDialogs.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CPlayerInterface.h"
#include "GameInstance.h"
#include "GameEngine.h"
#include "gui/WindowHandler.h"
#include "windows/CommandEnhancementWindow.h"
#include "windows/CHeroWindow.h"
#include "windows/HeroGrowthWindow.h"
#include "windows/HeroMasteryWindow.h"
#include "windows/CCreatureWindow.h"
#include "media/ISoundPlayer.h"
#include "../lib/CSoundBase.h"
#include "../lib/ConditionalWait.h"
#include "../lib/callback/CCallback.h"
#include "../lib/entities/hero/NewHorizonsCommandOffer.h"
#include "../lib/mapObjects/CGHeroInstance.h"

void CPlayerInterface::heroGotMastery(const newHorizonsHeroes::MasteryOffer & offer, QueryID queryID)
{
	// Copy the captured offer before queuing; never read a later hero level.
	auto show = [this, offer, queryID]()
	{
		closeActiveLevelUpDialog();
		auto submit = [this, hero = offer.hero, sequence = offer.sequence, queryID](int choice)
		{
			return cb->chooseHeroMastery(hero, queryID, sequence, choice);
		};
		showingDialog->setBusy();
		ENGINE->windows().createAndPushWindow<HeroMasteryWindow>(offer, queryID, std::move(submit));
	};
	createAndQueueDialog(PendingDialog::Type::Blocking, std::move(show), queryID);
	tryShowNextPendingDialog();
}

void CPlayerInterface::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	auto showCallback = [this, commander, skills = std::move(skills), queryID]() mutable
	{
		ENGINE->sound().playSound(soundBase::heroNewLevel);
		auto callback = [this, queryID](ui32 selection)
		{
			if(queryID < 0)
				return;
			cb->selectionMade(selection, queryID);
		};
		closeActiveLevelUpDialog();
		auto levelWindow = std::make_shared<CStackWindow>(commander, skills, callback);
		levelWindow->setCloseOnSelection(queryID < 0);
		ENGINE->windows().pushWindow(levelWindow);
	};
	createAndQueueDialog(PendingDialog::Type::Blocking, std::move(showCallback), queryID);
	tryShowNextPendingDialog();
}

void CPlayerInterface::heroGotCommandEnhancement(const newHorizonsHeroes::CommandEnhancementOffer & offer, QueryID queryID)
{
	if(offer.player != playerID)
		return;
	CommandEnhancementOfferView view;
	view.hero = offer.hero;
	view.milestone = offer.milestone;
	view.acquisitionLevel = offer.acquisitionLevel;
	view.recipient = offer.recipient;
	const auto * hero = cb->getHero(offer.hero);
	view.heroName = hero ? GAME->translator().translate(hero->getNameTextID())
		: "Hero " + std::to_string(offer.hero.getNum());
	for(size_t i = 0; i < offer.options.size(); ++i)
		view.options[i] = {offer.options[i].enhancement, offer.options[i].contribution};
	auto submit = [callback = cb, hero = offer.hero, queryID, milestone = offer.milestone](int choice)
	{
		return callback->chooseCommandEnhancement(hero, queryID, milestone, choice);
	};
	queueCommandEnhancement(std::move(view), queryID, std::move(submit));
}

void CPlayerInterface::queueCommandEnhancement(CommandEnhancementOfferView offer, QueryID query,
	std::function<int(int)> submit)
{
	if(findPendingDialog(query) != dialogs.end())
		return;
	// The adapter supplies the captured offer and an asynchronous, identity-bound reply.
	// Neither a deferred window nor a redraw reads the hero's later acquisition level.
	auto show = [this, offer = std::move(offer), query, submit = std::move(submit)]()
	{
		closeActiveLevelUpDialog();
		showingDialog->setBusy();
		ENGINE->windows().createAndPushWindow<CommandEnhancementWindow>(offer, query, submit);
	};
	createAndQueueDialog(PendingDialog::Type::Blocking, std::move(show), query);
	tryShowNextPendingDialog();
}

void CPlayerInterface::commandProgressionChanged(ObjectInstanceID heroID)
{
	const auto * hero = cb->getHero(heroID);
	if(!hero)
		return;
	for(const auto & holder : ENGINE->windows().findWindows<IGarrisonHolder>())
		if(holder->holdsGarrison(hero))
			holder->updateGarrisons();
	for(const auto & window : ENGINE->windows().findWindows<HeroGrowthWindow>())
		window->refresh(*hero);
	for(const auto & window : ENGINE->windows().findWindows<CHeroWindow>())
		if(window->curHero && window->curHero->id == heroID)
			window->refreshCommandPresentation();
}
