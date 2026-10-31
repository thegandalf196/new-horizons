/*
 * BattleBonusSerialization.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "../CStack.h"

void BattleInfo::validateOwnedBonusSerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported) return;
	validateExportedBonusSerialization(context);
	// Retained dead stacks are serialized too. Side hero/army references are IDs.
	for(const auto & stack : stacks)
		if(stack) stack->validateExportedBonusSerialization(context);
}
