/*
 * Part of VCMI / New Horizons.
 * Authors: listed in file AUTHORS in main folder.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PacksForLobby.h"
#include "../StartInfo.h"
#include "../gameState/CGameState.h"

void LobbyStartGame::validateNewHorizonsIndividualSpecialtiesSerialization(bool supported, bool ridersSupported, bool schoolSupported, bool additionalSupported, bool recoverySupported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsIndividualSpecialtiesSerialization(supported, ridersSupported, schoolSupported, additionalSupported, recoverySupported);
}

void LobbyStartGame::validateNewHorizonsSpecialtySerialization(
	const newHorizonsHeroes::SpecialtySerializationContext & context) const
{
	// This preflight runs before either lobby payload. Include policy3 in the
	// enclosing guard instead of waiting for a nested hero/world writer to fail.
	if(initializedStartInfo)
		initializedStartInfo->validateCommandSerialization(context.commandAuthority, context.leadershipEfficiency);
	if(initializedGameState)
		initializedGameState->validateCommandSerialization(context.commandAuthority, context.leadershipEfficiency);
	if(initializedGameState)
		initializedGameState->validateNewHorizonsSpecialtySerialization(context);
}
