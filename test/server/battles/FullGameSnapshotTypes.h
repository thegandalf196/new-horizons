/*
 * FullGameSnapshotTypes.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

// BinarySerializer instantiates visitors for the complete game-state graph.
// Snapshot tests must provide definitions, not only forward declarations.
#include "../../../lib/CPlayerState.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/Quest.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rmg/CMapGenOptions.h"
