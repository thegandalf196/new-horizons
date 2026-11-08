/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleAI.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/logging/VisualLogger.h"

CBattleAI::CBattleAI(heroActionSelection::Observer * observer, VinesCandidateValueReuse::TestControl * vinesReuse,
	battleSearch::Profile profile)
	: heroValueObserver(observer), vinesReuseControl(vinesReuse), searchProfile(profile), side(BattleSide::NONE),
	wasWaitingForRealize(false)
{
}

namespace
{
void logHexNumbers()
{
#if BATTLE_TRACE_LEVEL >= 1
	logVisual->updateWithLock("hexes", [](IVisualLogBuilder & b)
		{
			for(BattleHex hex = BattleHex(0); hex < GameConstants::BFIELD_SIZE; ++hex)
				b.addText(hex, std::to_string(hex.toInt()));
		});
#endif
}

}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	playerID = *CB->getPlayerID();
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false;
	movesSkippedByDefense = 0;

	logHexNumbers();
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences)
{
	initBattleInterface(ENV, CB);
	autobattlePreferences = autocombatPreferences;
}
