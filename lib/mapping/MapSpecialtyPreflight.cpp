/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CMap.h"
#include "../GameSettings.h"
#include "../mapObjects/CGHeroInstance.h"

void CMap::validateNewHorizonsIndividualSpecialtiesSerialization(bool supported, bool ridersSupported,
	bool schoolSupported, bool additionalSupported, bool recoverySupported) const
{
	validateNewHorizonsSpecialtySerialization({.fortuneAndArcane = supported,
		.individualRiders = ridersSupported, .schoolSpellcraft = schoolSupported,
		.additional = additionalSupported, .recovery = recoverySupported});
}

void CMap::validateNewHorizonsSpecialtySerialization(
	const newHorizonsHeroes::SpecialtySerializationContext & context) const
{
	if(gameSettings)
		gameSettings->validateNewHorizonsSpecialtySerialization(context);
	const auto validateHero = [&context](const CGHeroInstance & hero)
	{
		hero.validateRecoverySpecialtyStateSerialization(context.recovery);
		newHorizonsHeroes::validateCapturedSpecialtiesSerialization(hero.getPrimaryGrowthRules(), context);
	};
	for(const auto & object : objects)
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			validateHero(*hero);
	for(const auto & hero : heroesPool)
		if(hero)
			validateHero(*hero);
}
