/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "RecruitmentRowPlan.h"
#include "LeadershipEvaluation.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownRecruitmentOffers.h"
#include <limits>

namespace NK2AI
{
namespace
{
std::optional<RecruitmentRowPlan> planChoice(const CCreatureSet & army,
	const CGHeroInstance * carrier, const CGDwelling & source, CreatureID creature,
	int available, const TResources & resources, RecruitmentProjection projection,
	bool allowDisband)
{
	RecruitmentRowPlan result;
	auto & ci = result.creature;
	ci.creID = creature;
	ci.count = source.getRecruitmentAmount(creature, resources, available, carrier);
	if(ci.count <= 0)
		return std::nullopt;
	if(carrier && carrier->getArmyCommand())
	{
		const auto plan = newHorizonsHeroes::planCommandRecruitment(*carrier, projection.slots, creature, ci.count);
		if(!plan)
			return std::nullopt;
		projection.slots = plan->slots;
		result.projection = std::move(projection);
		result.cost = source.getTotalRecruitmentCost(creature, ci.count, carrier);
		return result;
	}
	const auto capacity = carrier ? carrier->getLeadershipSlotCapacity(creature) : std::nullopt;
	const std::optional<int64_t> maximum = capacity ? std::optional<int64_t>(capacity->maximum) : std::nullopt;
	SlotID dst = newHorizonsHeroes::recruitmentSlot(projection.slots, creature, ci.count, maximum);
	const auto present = std::ranges::find(projection.slots, dst, &newHorizonsHeroes::RecruitmentSlot::slot);
	const int64_t countPresent = present != projection.slots.end() ? present->count : 0;
	if(maximum)
		ci.count = static_cast<int>(std::min<int64_t>(ci.count, std::max<int64_t>(0, *maximum - countPresent)));
	if(ci.count <= 0)
		return std::nullopt;

	const auto * town = source.ID == Obj::TOWN ? dynamic_cast<const CGTownInstance *>(&source) : nullptr;
	SlotID leastValuableSlot;
	TResources leastValuableValue;
	leastValuableValue[6] = std::numeric_limits<int>::max();
	const bool disband = !dst.validSlot();
	if(disband)
	{
		if(!allowDisband)
			return std::nullopt;
		for(const auto & [slot, stack] : army.Slots())
		{
			if(projection.disbanded.contains(slot) || stack->getCreatureID() == CreatureID::NONE)
				continue;
			if(town && stack->getCreatureID().toCreature()->getFactionID() == town->getFactionID())
				continue;
			const auto value = stack->getCreatureID().toCreature()->getFullRecruitCost() * stack->getCount();
			if(value.marketValue() < leastValuableValue.marketValue())
			{
				leastValuableValue = value;
				leastValuableSlot = slot;
			}
		}
		const auto newValue = creature.toCreature()->getFullRecruitCost() * ci.count;
		if(!leastValuableSlot.validSlot() || newValue.marketValue() <= leastValuableValue.marketValue())
			return std::nullopt;
		dst = leastValuableSlot;
		projection.disbanded.insert(dst);
		ci.count -= leastValuableValue / creature.toCreature()->getFullRecruitCost();
		if(ci.count <= 0)
			return std::nullopt;
	}
	const auto destination = std::ranges::find(projection.slots, dst, &newHorizonsHeroes::RecruitmentSlot::slot);
	if(destination == projection.slots.end())
		projection.slots.push_back({dst, creature, ci.count});
	else if(disband)
		*destination = {dst, creature, ci.count};
	else
		destination->count += ci.count;
	result.projection = std::move(projection);
	result.cost = source.getTotalRecruitmentCost(creature, ci.count, carrier);
	return result;
}
}

std::optional<RecruitmentRowPlan> planRecruitmentRow(const CCreatureSet & army,
	const CGHeroInstance * carrier, const CGDwelling & source,
	const townRecruitmentOffers::RowOffer & offer, int available,
	const TResources & resources, const RecruitmentProjection & projection, bool allowDisband)
{
	if(offer.source != source.id || offer.row >= source.creatures.size()
		|| source.creatures[offer.row].second.empty())
		return std::nullopt;
	const auto & ordinary = source.creatures[offer.row].second;
	std::vector<CreatureID> choices{ordinary.back()}; // Preserve the ordinary winner and tie order.
	if(offer.choices != ordinary)
		for(const auto choice : offer.choices)
			if(!vstd::contains(choices, choice))
				choices.push_back(choice);

	std::optional<RecruitmentRowPlan> best;
	uint64_t bestValue = 0;
	for(const auto choice : choices)
	{
		if(!vstd::contains(offer.choices, choice))
			continue;
		auto plan = planChoice(army, carrier, source, choice, available, resources, projection, allowDisband);
		if(!plan)
			continue;
		if(choices.size() == 1)
		{
			plan->creature.level = static_cast<int>(offer.row);
			return plan; // Ordinary rows retain their existing choice without new valuation queries.
		}
		const auto value = leadershipAI::stackPower(carrier, choice, plan->creature.count, &army);
		if(!best || value > bestValue)
		{
			plan->creature.level = static_cast<int>(offer.row); // Request uses source row, not creature tier.
			best = std::move(plan);
			bestValue = value;
		}
	}
	return best;
}
}
