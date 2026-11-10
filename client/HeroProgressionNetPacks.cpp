/*
 * HeroProgressionNetPacks.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Client.h"
#include "ClientNetPackVisitors.h"
#include "GameEngine.h"
#include "gui/WindowHandler.h"
#include "windows/HeroGrowthWindow.h"
#include "../lib/callback/CGameInterface.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/gameState/CGameState.h"

namespace
{
void notifyCommandProgression(CClient & client, ObjectInstanceID hero)
{
	// Copy: callbacks can change the installed interfaces while notifying them.
	// This is the after-state visitor; presentation reads the accepted receipt.
	const auto interfaces = client.playerint;
	for(const auto & entry : interfaces)
	{
		entry.second->invalidatePaths();
		entry.second->commandProgressionChanged(hero);
	}
}
}

void ApplyClientNetPackVisitor::visitHeroPerkChosen(HeroPerkChosen & pack)
{
	notifyCommandProgression(cl, pack.hero);
}

void ApplyClientNetPackVisitor::visitHeroCommandProgression(HeroCommandProgression & pack)
{
	notifyCommandProgression(cl, pack.hero);
}

void ApplyClientNetPackVisitor::visitHeroCommandEnhancementDialog(HeroCommandEnhancementDialog & pack)
{
	const auto playerInterface = cl.playerint.find(pack.offer.player);
	if(playerInterface != cl.playerint.end())
		playerInterface->second->heroGotCommandEnhancement(pack.offer, pack.queryID);
}

void ApplyClientNetPackVisitor::visitHeroCommandEnhancementChosen(HeroCommandEnhancementChosen & pack)
{
	// Accepted state is applied before this notification and before QueryResolved.
	// Neither this packet nor its request ACK completes the mandatory dialog.
	notifyCommandProgression(cl, pack.offer.hero);
}

void ApplyClientNetPackVisitor::visitHeroMasteryOffer(HeroMasteryOffer & pack)
{
	if(const auto * hero = cl.gameInfo().getHero(pack.offer.hero))
		for(const auto & window : ENGINE->windows().findWindows<HeroGrowthWindow>())
			window->refresh(*hero);
}

void ApplyClientNetPackVisitor::visitHeroMasteryDialog(HeroMasteryDialog & pack)
{
	const auto playerInterface = cl.playerint.find(pack.offer.player);
	if(playerInterface != cl.playerint.end())
		playerInterface->second->heroGotMastery(pack.offer, pack.queryID);
}

void ApplyClientNetPackVisitor::visitHeroMasteryChosen(HeroMasteryChosen & pack)
{
	// State is already applied; only QueryResolved completes the mandatory dialog.
	if(const auto * hero = cl.gameInfo().getHero(pack.hero))
		for(const auto & window : ENGINE->windows().findWindows<HeroGrowthWindow>())
			window->refresh(*hero);
}
