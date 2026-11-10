/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGDwelling.h"

namespace NK2AI
{
namespace
{
bool acceptsUpgrade(const CGHeroInstance * hero, SlotID slot, CreatureID creature, int count)
{
	return !hero || newHorizonsHeroes::armySlotAccepts(hero, slot, creature, count);
}
}

std::vector<StackUpgradeInfo> ArmyManager::getHillFortUpgrades(const CCreatureSet * army, const CGHeroInstance * carrier) const
{
	std::vector<StackUpgradeInfo> result;
	for(const auto & [slot, stack] : army->Slots())
	{
		const auto initial = stack->getCreatureID();
		auto upgrades = initial.toCreature()->upgrades;
		std::erase_if(upgrades, [&](CreatureID target) { return !acceptsUpgrade(carrier, slot, target, stack->getCount()); });
		if(upgrades.empty()) continue;
		const auto strongest = *vstd::maxElementByFun(upgrades, [](CreatureID id) { return id.toCreature()->getAIValue(); });
		StackUpgradeInfo upgrade(initial, strongest, stack->getCount());
		if(initial.toCreature()->getLevel() == 1) upgrade.cost = TResources();
		result.push_back(upgrade);
	}
	return result;
}

std::vector<StackUpgradeInfo> ArmyManager::getDwellingUpgrades(const CCreatureSet * army,
	const CGDwelling * dwelling, const CGHeroInstance * carrier) const
{
	std::vector<StackUpgradeInfo> result;
	for(const auto & [slot, stack] : army->Slots())
	{
		const auto initial = stack->getCreatureID();
		auto upgrades = initial.toCreature()->upgrades;
		std::erase_if(upgrades, [&](CreatureID target)
		{
			return !acceptsUpgrade(carrier, slot, target, stack->getCount())
				|| !std::ranges::any_of(dwelling->creatures, [target](const auto & row) { return vstd::contains(row.second, target); });
		});
		if(upgrades.empty()) continue;
		const auto strongest = *vstd::maxElementByFun(upgrades, [](CreatureID id) { return id.toCreature()->getAIValue(); });
		result.emplace_back(initial, strongest, stack->getCount());
	}
	return result;
}
}
