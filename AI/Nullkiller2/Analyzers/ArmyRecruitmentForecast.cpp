/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../Engine/Nullkiller.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include <limits>

namespace NK2AI
{
std::vector<creInfo> ArmyManager::getArmyAvailableToBuy(const CCreatureSet * hero, const CGDwelling * dwelling) const
{
	return getArmyAvailableToBuy(hero, dwelling, aiNk->getFreeResources());
}

std::vector<creInfo> ArmyManager::getArmyAvailableToBuy(
	const CCreatureSet * hero,
	const CGDwelling * dwelling,
	TResources availableRes,
	uint8_t turn,
	const CGHeroInstance * carrier) const
{
	std::vector<creInfo> creaturesInDwellings;
	if(!carrier)
		carrier = dynamic_cast<const CGHeroInstance *>(hero);
	std::vector<newHorizonsHeroes::RecruitmentSlot> projectedSlots;
	projectedSlots.reserve(GameConstants::ARMY_SIZE);
	for(const auto & [slot, stack] : hero->Slots())
		projectedSlots.push_back({slot, stack->getCreatureID(), stack->getCount()});
	auto calendar = cpsic->getCalendar();
	bool countGrowth = (calendar.getDayOfWeek() + turn) > calendar.getDaysInWeek();

	const CGTownInstance * town = dwelling->ID == Obj::TOWN
		? dynamic_cast<const CGTownInstance *>(dwelling)
		: nullptr;

	std::set<SlotID> alreadyDisbanded;

	for(int i = dwelling->creatures.size() - 1; i >= 0; i--)
	{
		auto ci = infoFromDC(dwelling->creatures[i]);

		if(ci.creID == CreatureID::NONE) continue;

		if(i < GameConstants::CREATURES_PER_TOWN && countGrowth)
		{
			const int64_t growth = town ? town->creatureGrowth(i) : dwelling->normalWeeklyGrowth(i);
			const int64_t headroom = std::numeric_limits<int>::max() - int64_t(ci.count);
			ci.count += static_cast<int>(std::min(growth, headroom));
		}

		if(!ci.count) continue;

		const auto recruitCost = dwelling->getRecruitmentCost(ci.creID);
		if(!recruitCost.empty())
			vstd::amin(ci.count, availableRes / recruitCost);
		if(ci.count <= 0)
			continue;

		const auto capacity = carrier ? carrier->getLeadershipSlotCapacity(ci.creID) : std::nullopt;
		const std::optional<int64_t> maximum = capacity
			? std::optional<int64_t>(capacity->maximum) : std::nullopt;
		SlotID dst = newHorizonsHeroes::recruitmentSlot(projectedSlots, ci.creID, ci.count, maximum);
		const auto projected = std::ranges::find(projectedSlots, dst, &newHorizonsHeroes::RecruitmentSlot::slot);
		const int64_t alreadyPresent = projected != projectedSlots.end() ? projected->count : 0;
		if(maximum)
			ci.count = static_cast<int>(std::min<int64_t>(ci.count,
				std::max<int64_t>(0, *maximum - alreadyPresent)));

		if(ci.count <= 0)
			continue;

		// Calculate the value after stock, resource, and Leadership limits. This
		// avoids valuing an impossible replacement with the dwelling's full stock.
		TResources newStackValue = ci.creID.toCreature()->getFullRecruitCost() * ci.count;

		// Keep track of the least valuable slot in the hero's army
		SlotID leastValuableSlot;
		TResources leastValuableStackValue;
		leastValuableStackValue[6] = std::numeric_limits<int>::max();
		bool shouldDisband = false;
		if(!dst.validSlot()) // No free or matching slot; consider replacing
		{
			// Check for the least valuable existing stack
			for (auto& slot : hero->Slots())
			{
				if (alreadyDisbanded.find(slot.first) != alreadyDisbanded.end())
					continue;

				if(slot.second->getCreatureID() != CreatureID::NONE)
				{
					TResources currentStackValue = slot.second->getCreatureID().toCreature()->getFullRecruitCost() * slot.second->getCount();

					if (town && slot.second->getCreatureID().toCreature()->getFactionID() == town->getFactionID())
						continue;

					if(currentStackValue.marketValue() < leastValuableStackValue.marketValue())
					{
						leastValuableStackValue = currentStackValue;
						leastValuableSlot = slot.first;
					}
				}
			}

			// Decide whether to replace the least valuable stack
			if(!leastValuableSlot.validSlot() || newStackValue.marketValue() <= leastValuableStackValue.marketValue())
			{
				continue; // Skip if the new stack isn't worth replacing
			}
			else
			{
				shouldDisband = true;
				dst = leastValuableSlot;
			}
		}

		int disbandMalus = 0;

		if (shouldDisband)
		{
			disbandMalus = leastValuableStackValue / ci.creID.toCreature()->getFullRecruitCost();
			alreadyDisbanded.insert(leastValuableSlot);
		}

		ci.count -= disbandMalus;

		if(ci.count <= 0)
			continue;

		ci.level = i; //this is important for Dungeon Summoning Portal
		creaturesInDwellings.push_back(ci);
		const auto destination = std::ranges::find(projectedSlots, dst, &newHorizonsHeroes::RecruitmentSlot::slot);
		if(destination == projectedSlots.end())
			projectedSlots.push_back({dst, ci.creID, ci.count});
		else if(shouldDisband)
			*destination = {dst, ci.creID, ci.count};
		else
			destination->count += ci.count;
		availableRes -= recruitCost * ci.count;
	}

	return creaturesInDwellings;
}

}
