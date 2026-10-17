/*
 * NativeSummonHealthQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsElementalRebirth.h"
#include "SummonHealthQuery.h"
#include "../CStack.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"

namespace newHorizonsElementalRebirth
{
namespace
{
int32_t effectiveSummonMaxHPImpl(const CArmedInstance * sourceArmy,
	CreatureID creatureId, PlayerColor owner, BattleSide side, std::optional<UnitQueryTerrain> terrain)
{
	if(!creatureId.hasValue())
		return 0;
	const auto * creature = creatureId.toCreature();
	if(!creature || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return 0;

	const auto capturedTerrain = terrain ? *terrain : captureUnitQueryTerrain(sourceArmy);
	CStackBasicDescriptor descriptor(creature, 1);
	CStack hypothetical(&descriptor, owner, -1, side, SlotID::SUMMONED_SLOT_PLACEHOLDER, true, capturedTerrain);
	hypothetical.summoned = true;
	hypothetical.natureSummoned = false;
	if(sourceArmy)
		hypothetical.attachToSource(*sourceArmy);
	hypothetical.attachToSource(*creature);
	return querySummonMaxHP(hypothetical);
}
}

int32_t effectiveSummonMaxHP(const CArmedInstance * sourceArmy,
	CreatureID creatureId, PlayerColor owner, BattleSide side)
{
	return effectiveSummonMaxHPImpl(sourceArmy, creatureId, owner, side, std::nullopt);
}

int32_t effectiveSummonMaxHP(const CArmedInstance * sourceArmy,
	CreatureID creatureId, PlayerColor owner, BattleSide side, UnitQueryTerrain terrain)
{
	return effectiveSummonMaxHPImpl(sourceArmy, creatureId, owner, side, terrain);
}
}
