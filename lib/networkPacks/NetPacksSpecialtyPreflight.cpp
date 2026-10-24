/*
 * Part of VCMI / New Horizons.
 * Authors: listed in file AUTHORS in main folder.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PacksForLobby.h"
#include "../gameState/CGameState.h"

void LobbyStartGame::validateNewHorizonsIndividualSpecialtiesSerialization(bool supported, bool ridersSupported, bool schoolSupported, bool additionalSupported, bool recoverySupported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsIndividualSpecialtiesSerialization(supported, ridersSupported, schoolSupported, additionalSupported, recoverySupported);
}

void LobbyStartGame::validateNewHorizonsSpecialtySerialization(
	const newHorizonsHeroes::SpecialtySerializationContext & context) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsSpecialtySerialization(context);
}
