/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsElusive.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/hero/NewHorizonsRampartSpecialties.h"
#include "../mapObjects/CGCreature.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsElusive
{
bool ignoresGuard(const IGameInfoCallback & gameInfo, const CGHeroInstance & hero,
	const CGObjectInstance & guardian, const int3 & tile)
{
	if(guardian.passableFor(&hero))
		return true;
	const auto rank = newHorizonsHeroes::elusiveGuardRank(&hero);
	const auto * creature = dynamic_cast<const CGCreature *>(&guardian);
	if(!rank || !creature || tile == guardian.visitablePos()
		|| (guardian.getOwner() != PlayerColor::NEUTRAL && guardian.getOwner() != PlayerColor::UNFLAGGABLE))
		return false;
	const int guard = newHorizonsHeroes::specialtyRank(gameInfo.getCreatureCategoryRules(), creature->getCreatureID());
	return guard >= 0 && guard <= static_cast<int>(*rank);
}
}
