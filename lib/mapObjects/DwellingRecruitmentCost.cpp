/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGDwelling.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstates.h"
#include "../CCreatureHandler.h"

TResources CGDwelling::getRecruitmentCost(CreatureID creatureId) const
{
	const auto * creature = creatureId.toCreature();
	if(!creature)
		return {};

	if((ID == Obj::CREATURE_GENERATOR1 || ID == Obj::CREATURE_GENERATOR4) && creature->getLevel() == 1)
		return {};

	return creature->getFullRecruitCost();
}

TResources CGDwelling::getTotalRecruitmentCost(CreatureID creature, int32_t amount, const CGHeroInstance * buyer) const
{
	const bool discount = buyer && buyer->getOwner().isValidPlayer()
		&& buyer->hasActivePerk("new-horizons:estates", "new-horizons:estates.conscription");
	return newHorizonsEstates::recruitmentCost(getRecruitmentCost(creature), amount, discount);
}

int32_t CGDwelling::getRecruitmentAmount(CreatureID creature, const ResourceSet & resources,
	int32_t available, const CGHeroInstance * buyer) const
{
	const bool discount = buyer && buyer->getOwner().isValidPlayer()
		&& buyer->hasActivePerk("new-horizons:estates", "new-horizons:estates.conscription");
	return newHorizonsEstates::affordableRecruitment(getRecruitmentCost(creature), resources, available, discount);
}
