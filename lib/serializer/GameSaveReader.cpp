/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "GameSaveReader.h"
#include "CLoadFile.h"
#include "../StartInfo.h"
#include "../CPlayerState.h"
#include "../GameSettings.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/Propagators.h"
#include "../bonuses/Updaters.h"
#include "../campaign/CampaignState.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../gameState/CGameState.h"
#include "../gameState/CGameStateCampaign.h"
#include "../gameState/QuestInfo.h"
#include "../gameState/TavernHeroesPool.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/MiscObjects.h"
#include "../mapping/CMap.h"
#include "../modding/ActiveModsInSaveList.h"
#include "../rmg/CMapGenOptions.h"

void gameSave::readGameData(CLoadFile & file, CGameState & world)
{
	logGlobal->info("Loading game state...");
	CMapHeader dummyHeader;
	ActiveModsInSaveList dummyActiveMods;
	file.load(dummyHeader);
	StartInfo dummyStartInfo;
	file.load(dummyStartInfo);
	file.load(dummyActiveMods);
	file.load(world);
}
