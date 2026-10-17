/*
 * UnitTransitionSidecars.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitTransitionSidecars.h"
#include "Unit.h"
#include "ReducedExtraActivationState.h"
#include "../spells/CSpell.h"
#include <type_traits>

namespace
{
bool orderUnitsAdjacent(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second)
		return false;
	for(const auto & firstHex : first->getHexes())
	{
		if(!firstHex.isValid())
			continue;
		for(const auto & secondHex : second->getHexes())
			if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	}
	return false;
}
}

bool battle::protectOrderIsSeparated(const Unit * protector, const Unit * ward)
{
	return !protector || !ward || !protector->alive() || !ward->alive() || !orderUnitsAdjacent(protector, ward);
}

void battle::clearReducedActivationForUnit(ReducedExtraActivationState & state, uint32_t id)
{
	static_assert(std::is_nothrow_copy_assignable_v<ReducedExtraActivationState>);
	auto extraActivation = state;
	if(extraActivation.activeUnitId != id)
		return;
	extraActivation.activeUnitId = ReducedExtraActivationState::INVALID_UNIT_ID;
	extraActivation.outputPercent = 100;
	state = extraActivation;
}

bool battle::isTransientSpellEffect(const Bonus * b)
{
	//Special case: persistent effects, such as DISRUPTING_RAY, survive death
	return b->source == BonusSource::SPELL_EFFECT && !b->sid.as<SpellID>().toSpell()->isPersistent();
}
