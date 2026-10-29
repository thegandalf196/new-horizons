/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"
#include "ArmyUpgradeSelection.h"
#include "../../../lib/entities/hero/NewHorizonsGhostSpecialty.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/gameState/UpgradeInfo.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
namespace NK2AI
{
bool AIGateway::makePossibleUpgrades(const CArmedInstance * army)
{
	if(!army)
		return false;
	bool upgraded = false;
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		const auto * stack = army->getStackPtr(SlotID(slot));
		if(!stack)
			continue;
		const auto * carrier = dynamic_cast<const CGHeroInstance *>(army);
		while(true)
		{
			UpgradeInfo offers(stack->getId());
			cc->fillUpgradeInfo(army, SlotID(slot), offers);
			const auto selected = armyUpgrade::choose(*stack, offers, carrier, nullkiller->getFreeResources());
			if(!selected)
				break;
			cc->upgradeCreature(army, SlotID(slot), *selected);
			if(stack->getId() != *selected)
				break; // A rejected or unobserved request cannot repeat forever.
			upgraded = true;
			logAi->debug("Upgraded %d %s to %s", stack->getCount(), offers.oldID.toCreature()->getNamePluralTranslated(),
				selected->toCreature()->getNamePluralTranslated());
		}
	}
	return upgraded;
}
void AIGateway::upgradeGhosts()
{
	const CreatureID ghost(CreatureID::decode("new-horizons:ghost"));
	for(const auto * hero : cc->getHeroesInfo())
	{
		if(!hero || hero->tempOwner != playerID || !newHorizonsHeroes::hasGhostUpgrade(*hero))
			continue;
		for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		{
			const auto * stack = hero->getStackPtr(SlotID(slot));
			if(!stack || stack->getId() != CreatureID(CreatureID::decode("core:wraith")))
				continue;
			UpgradeInfo offers(stack->getId());
			cc->fillUpgradeInfo(hero, SlotID(slot), offers);
			const auto selected = armyUpgrade::choose(*stack, offers, hero, TResources());
			if(selected && *selected == ghost)
				cc->upgradeCreature(hero, SlotID(slot), ghost);
		}
	}
}

}
