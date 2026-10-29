/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "ArmyUpgradeSelection.h"
#include "../../../lib/entities/hero/NewHorizonsGhostSpecialty.h"
#include "../../../lib/gameState/UpgradeInfo.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
namespace NK2AI::armyUpgrade
{
namespace
{
bool recognizedFreeBenefit(const CStackInstance & stack, const UpgradeInfo & offers,
	const CGHeroInstance * carrier, CreatureID upgrade)
{
	return carrier && newHorizonsHeroes::hasGhostUpgrade(*carrier)
		&& stack.getId() == CreatureID(CreatureID::decode("core:wraith"))
		&& upgrade == CreatureID(CreatureID::decode("new-horizons:ghost"))
		&& offers.getUpgradeCostsFor(upgrade) == TResources();
}
}
std::optional<CreatureID> choose(const CStackInstance & stack, const UpgradeInfo & offers,
	const CGHeroInstance * carrier, const TResources & available)
{
	std::vector<CreatureID> viable;
	for(const auto upgrade : offers.getAvailableUpgrades())
	{
		const auto capacity = carrier ? carrier->getLeadershipSlotCapacity(upgrade) : std::nullopt;
		if(!capacity || capacity->accepts(stack.getCount()))
			viable.push_back(upgrade);
	}
	if(viable.empty())
		return std::nullopt;
	CreatureID selected = *vstd::maxElementByFun(viable, [](const CreatureID & id)
	{
		return id.toCreature()->getAIValue();
	});
	const auto oldValue = stack.getCreature()->getAIValue();
	const auto newValue = selected.toCreature()->getAIValue();
	if(newValue == oldValue)
		for(const auto candidate : viable)
			if(candidate.toCreature()->getAIValue() == newValue
				&& recognizedFreeBenefit(stack, offers, carrier, candidate))
			{
				selected = candidate;
				break;
			}
	if((newValue > oldValue || (newValue == oldValue && recognizedFreeBenefit(stack, offers, carrier, selected)))
		&& available.canAfford(offers.getUpgradeCostsFor(selected) * stack.getCount()))
		return selected;
	return std::nullopt;
}
}
