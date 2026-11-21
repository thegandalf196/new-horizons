/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForcedMarchWeek.h"

#include "../CGameHandler.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/entities/hero/NewHorizonsCastleAdventureSpecialties.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"

namespace newHorizonsForcedMarch
{
void grantWeeklyMoralePenalties(CGameHandler & handler)
{
	for(const auto & [color, player] : handler.gameState().players)
		for(const auto * hero : player.getHeroes())
			if(const auto penalty = newHorizonsHeroes::forcedMarchMoralePenalty(*hero))
			{
				GiveBonus grant(GiveBonus::ETarget::OBJECT, hero->id, *penalty);
				handler.giveHeroBonus(&grant);
			}
}
}
