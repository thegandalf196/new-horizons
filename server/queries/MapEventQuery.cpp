/*
 * MapEventQuery.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapEventQuery.h"

#include "QueriesProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/gameState/CGameState.h"

#include <vcmi/scripting/MapEventDispatcher.h>

MapEventQuery::MapEventQuery(CGameHandler * owner, PlayerColor player):
	CQuery(owner, TYPE)
{
	addPlayer(player);
}

void MapEventQuery::setContinuation(int handle)
{
	continuationHandle = handle;
}

void MapEventQuery::setPendingAnswer(std::optional<int32_t> answer)
{
	pendingAnswer = answer;
}

void MapEventQuery::setVisitingHero(ObjectInstanceID hero)
{
	visitingHero = hero;
}

void MapEventQuery::onExposure(QueryPtr topQuery)
{
	auto * dispatcher = gh->gameState().getMapEventDispatcher();

	// If the hero lost a scripted combat it no longer exists; abandon the continuation rather than resume
	// a handler whose captured hero is gone.
	bool heroGone = visitingHero.hasValue() && gh->gameInfo().getHero(visitingHero) == nullptr;

	if(!dispatcher || heroGone)
	{
		owner->popIfTop(*this);
		return;
	}

	// Resuming may spawn a new child query (another blocking action); in that case the continuation is
	// not finished and this query stays on the stack under the freshly-added child.
	bool finished = dispatcher->resumeCoroutine(*gh, continuationHandle, pendingAnswer);
	pendingAnswer.reset();

	if(finished)
		owner->popIfTop(*this);
}
