/*
 * CUnitStateValues.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "UnitStateValueExchange.h"
#include "UnitStateValueFields.h"

namespace battle
{
CUnitState & CUnitState::operator=(const CUnitState & other)
{
	//do not change unit and bonus info
	const CreatureID previousEffectiveCreature = battleFormCreature();
	const CreatureID previousOriginalCreature = battleFormOriginalCreature();

	UnitStateValueFields::copy(*this, other);
	if(previousEffectiveCreature != battleFormCreature()
		|| previousOriginalCreature != battleFormOriginalCreature())
		onBattleFormChanged();
	return *this;
}

void exchangePreparedUnitStateValues(CUnitState & destination, CUnitState & prepared) noexcept
{
	UnitStateValueFields::exchange(destination, prepared);
}
}
