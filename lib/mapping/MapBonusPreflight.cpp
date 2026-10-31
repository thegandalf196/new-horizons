/*
 * MapBonusPreflight.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CMap.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapObjects/TownBuildingInstance.h"
#include "../rewardable/Interface.h"

void CGHeroInstance::validateHeroBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported) return;
	validateArmyBonusSerialization(context);
	if(commander) commander->validateExportedBonusSerialization(context);
}

void CGTownInstance::validateTownBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported) return;
	validateArmyBonusSerialization(context);
	townAndVis.validateExportedBonusSerialization(context);
	for(const auto & [building, instance] : rewardableBuildings)
		if(instance) instance->configuration.validateBonusSerialization(context);
}

void CMap::validateOwnedBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported) return;
	for(const auto & object : objects)
	{
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			hero->validateHeroBonusSerialization(context);
		else if(const auto * town = dynamic_cast<const CGTownInstance *>(object.get()))
			town->validateTownBonusSerialization(context);
		else if(const auto * army = dynamic_cast<const CArmedInstance *>(object.get()))
			army->validateArmyBonusSerialization(context);
		else if(const auto * node = dynamic_cast<const CBonusSystemNode *>(object.get()))
			node->validateExportedBonusSerialization(context);
		if(const auto * rewardable = dynamic_cast<const Rewardable::Interface *>(object.get()))
			rewardable->configuration.validateBonusSerialization(context);
	}
	for(const auto & hero : heroesPool)
		if(hero) hero->validateHeroBonusSerialization(context);
	for(const auto & artifact : artInstances)
		if(artifact) artifact->validateExportedBonusSerialization(context);
}
