/*
 * SummonedCreatureHealth.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/UnitQueryTerrain.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"

namespace spells
{
int32_t Mechanics::getSummonedCreatureMaxHealth(const Creature * creature, const bool natureSummoned) const
{
	if(!creature)
		return 1;

	const auto * engineCreature = dynamic_cast<const CCreature *>(creature);
	if(!engineCreature)
		return std::max(1, static_cast<int32_t>(creature->getMaxHealth()));

	// A hypothetical node inherits the same source bonuses and applies the same
	// limiter pipeline as a real CStack. Source-only links avoid registering a
	// live child or invalidating caches on the hero/creature graph.
	const auto * battleState = battle() ? battle()->getBattle() : nullptr;
	const auto * army = battleState ? battleState->getSideArmy(getCasterSide()) : nullptr;
	const CArmedInstance * sourceArmy = army ? army : getHeroCaster();
	// Capture only the source's actual bound battle terrain. This value does not
	// grant the hypothetical battle/roster/updater membership or an adventure tile.
	const auto terrain = captureUnitQueryTerrain(sourceArmy);
	CStackBasicDescriptor descriptor(engineCreature, 1);
	CStack hypothetical(&descriptor, getCasterColor(), -1, getCasterSide(),
		SlotID::SUMMONED_SLOT_PLACEHOLDER, true, terrain);
	hypothetical.summoned = true;
	hypothetical.natureSummoned = natureSummoned;
	if(sourceArmy)
		hypothetical.attachToSource(*sourceArmy);
	hypothetical.attachToSource(*engineCreature);

	return std::max(1, hypothetical.valOfBonuses(BonusType::STACK_HEALTH));
}
}
