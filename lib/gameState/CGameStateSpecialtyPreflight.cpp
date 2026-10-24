/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameState.h"
#include "../mapping/CMap.h"

void CGameState::validateNewHorizonsIndividualSpecialtiesSerialization(bool supported, bool ridersSupported,
	bool schoolSupported, bool additionalSupported, bool recoverySupported) const
{
	validateNewHorizonsSpecialtySerialization({.fortuneAndArcane = supported,
		.individualRiders = ridersSupported, .schoolSpellcraft = schoolSupported,
		.additional = additionalSupported, .recovery = recoverySupported});
}

void CGameState::validateNewHorizonsSpecialtySerialization(
	const newHorizonsHeroes::SpecialtySerializationContext & context) const
{
	newHorizonsHeroes::validateCapturedSpecialtiesSerialization(heroDevelopmentRules, context);
	if(map)
		map->validateNewHorizonsSpecialtySerialization(context);
}
