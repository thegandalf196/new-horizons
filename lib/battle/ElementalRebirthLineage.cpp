/*
 * ElementalRebirthLineage.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ElementalRebirthLineage.h"
#include "Unit.h"
#include <algorithm>

namespace newHorizonsElementalRebirth
{
const std::array<CreatureID, 5> & canonicalElementals()
{
	static const std::array<CreatureID, 5> ids = {
		CreatureID(CreatureID::decode("core:airElemental")),
		CreatureID(CreatureID::decode("core:waterElemental")),
		CreatureID(CreatureID::decode("core:fireElemental")),
		CreatureID(CreatureID::decode("core:earthElemental")),
		CreatureID(CreatureID::decode("core:magicElemental"))
	};
	return ids;
}

bool isCanonicalElemental(CreatureID creature)
{
	const auto & ids = canonicalElementals();
	return creature.hasValue() && creature.toCreature()
		&& std::find(ids.begin(), ids.end(), creature) != ids.end();
}

bool isFirstGenerationOutput(const battle::Unit & unit)
{
	return unit.unitType() && unit.isSummoned() && !unit.isClone()
		&& unit.getPhantomInitialIntegrity() == 0
		&& unit.getRebirthOriginalAggregateHP() > 0
		&& isCanonicalElemental(unit.creatureId());
}
}
