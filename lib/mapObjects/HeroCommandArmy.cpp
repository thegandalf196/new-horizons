/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/creature/NewHorizonsAssimilation.h"
#include "../entities/hero/NewHorizonsCommandArmy.h"

newHorizonsHeroes::CommandCreatureTraits CGHeroInstance::getCommandCreatureTraits(CreatureID creature) const
{
	newHorizonsHeroes::CommandCreatureTraits traits;
	traits.assimilation = newHorizonsCreatures::hasIntrinsicAssimilation(creature);
	if(cb)
		if(const auto category = cb->getCreatureCategory(creature)) traits.category = category->category;
	return traits;
}

newHorizonsHeroes::CommandPlacementError CGHeroInstance::getCommandSlotEligibility(SlotID slot, CreatureID creature) const
{
	return newHorizonsHeroes::commandArmySlotEligibility(this, slot, creature);
}
