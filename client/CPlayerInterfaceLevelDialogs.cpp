/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CPlayerInterface.h"
#include "GameEngine.h"
#include "gui/WindowHandler.h"
#include "windows/CCreatureWindow.h"
#include "windows/CFourOfferLevelWindow.h"
#include "windows/CLevelWindow.h"
#include "windows/CommandEnhancementWindow.h"
#include "windows/HeroMasteryWindow.h"

void CPlayerInterface::closeActiveLevelUpDialog()
{
	if(auto window = ENGINE->windows().topWindow<CFourOfferLevelWindow>())
		window->close();
	else if(auto levelWindow = ENGINE->windows().topWindow<CLevelWindow>())
		levelWindow->close();
	else if(auto masteryWindow = ENGINE->windows().topWindow<HeroMasteryWindow>())
		masteryWindow->close(); // Refuses dismissal until its matching query resolves.
	else if(auto enhancementWindow = ENGINE->windows().topWindow<CommandEnhancementWindow>())
		enhancementWindow->close();
	else if(auto commanderWindow = ENGINE->windows().topWindow<CStackWindow>(); commanderWindow && commanderWindow->isCommanderLevelUpDialog())
		commanderWindow->close();
}

void CPlayerInterface::levelUpRequestApplied(uint32_t requestID, bool success)
{
	for(const auto & window : ENGINE->windows().findWindows<CLevelWindow>())
		window->rerollRequestApplied(requestID, success);
	for(const auto & window : ENGINE->windows().findWindows<CFourOfferLevelWindow>())
		window->requestApplied(requestID, success);
}

void CPlayerInterface::levelUpQueryResolved(QueryID queryID)
{
	for(const auto & window : ENGINE->windows().findWindows<CFourOfferLevelWindow>())
		window->queryResolved(queryID);
}

bool CPlayerInterface::drainFourOfferLevelDialogs()
{
	for(const auto & window : ENGINE->windows().findWindows<CFourOfferLevelWindow>())
	{
		if(!ENGINE->windows().isTopWindow(window.get()))
			return false;
		window->close();
		if(ENGINE->windows().isTopWindow(window.get()))
			return false;
	}
	return true;
}

void CPlayerInterface::refreshFourOfferLevelDialogs(const CGHeroInstance * hero)
{
	for(const auto & window : ENGINE->windows().findWindows<CFourOfferLevelWindow>())
		if(window->matchesHero(hero))
			window->refreshEligibility();
}
