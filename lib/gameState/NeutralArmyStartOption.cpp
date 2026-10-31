/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameState.h"
#include "../StartInfo.h"
#include "../GameSettings.h"
#include "../mapObjects/NewHorizonsNeutralArmies.h"
#include "../mapping/CMap.h"
#include "../mapping/CMapInfo.h"
#include "../networkPacks/PacksForLobby.h"
#include <stdexcept>

void ExtraOptionsInfo::validateNeutralArmySize() const
{
	if(neutralArmySizePercent && (*neutralArmySizePercent < 1 || *neutralArmySizePercent > 100))
		throw std::runtime_error("Neutral army start percentage must be in [1,100]");
}
void ExtraOptionsInfo::validateNeutralArmySizeSerialization(bool supported) const
{
	validateNeutralArmySize();
	if(!supported && neutralArmySizePercent)
		throw std::runtime_error("Neutral army start option requires format1147");
}
void newHorizonsNeutralArmies::applyStartOption(CMap & map, const StartInfo & start)
{
	start.extraOptionsInfo.validateNeutralArmySize();
	if(start.mode == EStartMode::NEW_GAME && !map.battleOnly && start.extraOptionsInfo.neutralArmySizePercent)
		map.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT,
			JsonNode(*start.extraOptionsInfo.neutralArmySizePercent));
}
void CMapInfo::validateNeutralArmyStartOptionSerialization(bool supported) const
{
	if(scenarioOptionsOfSave)
		scenarioOptionsOfSave->extraOptionsInfo.validateNeutralArmySizeSerialization(supported);
}
void LobbyState::validateNeutralArmyStartOptionSerialization(bool supported) const
{
	if(si) si->extraOptionsInfo.validateNeutralArmySizeSerialization(supported);
	if(mi) mi->validateNeutralArmyStartOptionSerialization(supported);
}
void CGameState::validateNeutralArmyStartOptionSerialization(bool supported) const
{
	if(scenarioOps) scenarioOps->extraOptionsInfo.validateNeutralArmySizeSerialization(supported);
	if(initialOpts) initialOpts->extraOptionsInfo.validateNeutralArmySizeSerialization(supported);
}
void LobbyStartGame::validateNeutralArmyStartOptionSerialization(bool supported) const
{
	if(initializedStartInfo)
		initializedStartInfo->extraOptionsInfo.validateNeutralArmySizeSerialization(supported);
	if(initializedGameState)
		initializedGameState->validateNeutralArmyStartOptionSerialization(supported);
}
