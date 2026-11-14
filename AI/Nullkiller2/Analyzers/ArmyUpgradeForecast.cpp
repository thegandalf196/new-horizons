/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../Helpers/LeadershipEvaluation.h"
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
		const auto strongest = *vstd::maxElementByFun(upgrades, [&](CreatureID id)
			{ return leadershipAI::stackPower(carrier, id, stack->getCount(), army); });
		StackUpgradeInfo upgrade(initial, strongest, stack->getCount());
		const auto before = leadershipAI::stackPower(carrier, initial, stack->getCount(), army);
		const auto after = leadershipAI::stackPower(carrier, strongest, stack->getCount(), army);
		upgrade.upgradeValue = after > before ? after - before : 0;
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
		const auto strongest = *vstd::maxElementByFun(upgrades, [&](CreatureID id)
			{ return leadershipAI::stackPower(carrier, id, stack->getCount(), army); });
		StackUpgradeInfo upgrade(initial, strongest, stack->getCount());
		const auto before = leadershipAI::stackPower(carrier, initial, stack->getCount(), army);
		const auto after = leadershipAI::stackPower(carrier, strongest, stack->getCount(), army);
		upgrade.upgradeValue = after > before ? after - before : 0;
		result.push_back(upgrade);
	}
	return result;
}
}
