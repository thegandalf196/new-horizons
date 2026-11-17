/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CMap.h"
#include "../GameSettings.h"
#include "../mapObjects/CGHeroInstance.h"

void CMap::validateNewHorizonsWeightedPerkSerialization(bool supported, bool r2Supported, bool strictSupported, bool optionalRerollSupported, bool passiveFactionSupported) const
{
	if(gameSettings)
		gameSettings->validateNewHorizonsWeightedPerkSerialization(supported, r2Supported, strictSupported, optionalRerollSupported, passiveFactionSupported);
	// Sealed captured catalogues were admitted once. Target profiles that omit
	// any dependent feature retain their original before-prefix checks.
	if(supported && r2Supported && strictSupported && optionalRerollSupported && passiveFactionSupported)
		return;
	for(const auto & object : objects)
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			newHorizonsHeroes::validateWeightedPerkSerialization(hero->getPerkState().getRules(), supported, r2Supported, strictSupported, optionalRerollSupported, passiveFactionSupported);
	for(const auto & hero : heroesPool)
		if(hero)
			newHorizonsHeroes::validateWeightedPerkSerialization(hero->getPerkState().getRules(), supported, r2Supported, strictSupported, optionalRerollSupported, passiveFactionSupported);
}
