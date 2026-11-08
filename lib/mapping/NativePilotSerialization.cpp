/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CMap.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../nativeRules/PreparedPilotRules.h"
void CMap::validateNativePilotSerialization(bool supported) const
{
	const auto validate = [&](const CGHeroInstance & hero)
	{
		// Current perk rows are admitted by the existing weighted-rule preflight.
		if(!supported)
			nativeRules::validatePilotPerks(hero.getPerkState().rules, false);
		nativeRules::validatePilotCreatures(hero.getPrimaryGrowthRules(), supported);
	};
	for(const auto & hero : heroesPool)
		if(hero) validate(*hero);
	for(const auto & object : objects)
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get())) validate(*hero);
}
