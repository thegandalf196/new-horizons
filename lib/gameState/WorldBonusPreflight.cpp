/*
 * WorldBonusPreflight.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGameState.h"
#include "../CPlayerState.h"
#include "../mapping/CMap.h"

void CGameState::validateOwnedBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported) return;
	if(map) map->validateOwnedBonusSerialization(context);
	for(const auto & [color, player] : players)
		player.validateOwnedBonusSerialization(context);
	for(const auto & [id, team] : teams)
		team.validateExportedBonusSerialization(context);
	globalEffects.validateExportedBonusSerialization(context);
	// Tavern pool entries, armies/heroes referenced by ID, and bonus parents
	// are not owned wire payloads. Ordinary world saves omit currentBattles.
}
