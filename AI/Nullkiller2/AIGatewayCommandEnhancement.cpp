/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AIGateway.h"
#include "../CommandEnhancementChoice.h"
#include "../../lib/callback/CCallback.h"

namespace NK2AI
{
void AIGateway::heroGotCommandEnhancement(const newHorizonsHeroes::CommandEnhancementOffer & offer, QueryID queryID)
{
	status.addQuery(queryID, "Command enhancement choice");
	executeActionAsync("heroGotCommandEnhancement", [this, offer, queryID]()
	{
		const auto * hero = cc->getHero(offer.hero);
		if(!hero || hero->getOwner() != offer.player)
		{
			logAi->error("Cannot answer Command query for a missing or transferred hero");
			return;
		}
		cc->chooseCommandEnhancement(offer.hero, queryID, offer.milestone, commandAI::chooseEnhancement(offer, *hero));
	});
}
}
