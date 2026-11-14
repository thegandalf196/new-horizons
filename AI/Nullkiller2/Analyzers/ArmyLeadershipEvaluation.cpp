/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../Helpers/LeadershipEvaluation.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/NewHorizonsDiscipline.h"
#include "../../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include <stdexcept>
#include <limits>

namespace NK2AI
{
void accumulateArmySupply(SlotInfo & total, const CCreature * creature, int count)
{
	if(!creature || count <= 0) return;
	total.creature = creature;
	total.count = static_cast<int>(std::min<int64_t>(std::numeric_limits<TQuantity>::max(),
		int64_t(total.count) + count));
	const auto power = static_cast<uint64_t>(std::max<int64_t>(0, creature->getAIValue()))
		* static_cast<uint64_t>(count);
	total.power = leadershipAI::addPower(total.power, power);
}

std::vector<SlotInfo> ArmyManager::getSortedSlots(const CCreatureSet * target, const CCreatureSet * source) const
{
	std::map<const CCreature *, int64_t> counts;
	for(const auto * army : {target, source})
		for(const auto & [slot, stack] : army->Slots())
			counts[stack->getCreature()] += stack->getCount();
	std::vector<SlotInfo> result;
	// Technical count overflow needs another physical stack. Leadership never
	// fragments a stack, discards excess creatures, or limits their acquisition.
	for(const auto & [creature, total] : counts)
		for(int64_t remaining = total; remaining > 0;)
		{
			const int count = static_cast<int>(std::min<int64_t>(remaining, std::numeric_limits<TQuantity>::max()));
			result.push_back({creature, count, evaluateStackPower(creature, count)});
			remaining -= count;
		}
	std::ranges::sort(result, [](const SlotInfo & left, const SlotInfo & right)
		{ return left.power > right.power; });
	return result;
}

std::vector<SlotInfo> ArmyManager::getLeadershipSortedSlots(const CCreatureSet * target,
	const CCreatureSet * source, const CGHeroInstance * hero) const
{
	auto slots = getSortedSlots(target, source);
	if(hero && newHorizonsHeroes::usesLeadershipEfficiency(hero->getCapabilityRules()))
	{
		for(auto & slot : slots)
			slot.power = leadershipAI::stackPower(hero, slot.creature->getId(), slot.count, target);
		std::ranges::stable_sort(slots, [](const SlotInfo & left, const SlotInfo & right)
			{ return left.power > right.power; });
	}
	return slots;
}

uint64_t ArmyManager::evaluateCompositionArmy(const CGHeroInstance * hero, const CCreatureSet & army,
	std::vector<SlotInfo> & slots) const
{
	const auto & badMorale = cpsic->getSettings().getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const auto & goodMorale = cpsic->getSettings().getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE);
	const int dice = cpsic->getSettings().getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	uint64_t value = 0;
	for(const auto & [slotId, stack] : army.Slots())
	{
		const auto adjustment = newHorizonsDiscipline::espritDeCorpsMoraleAdjustment(hero, *stack);
		const bool mercenary = hero && hero->hasActivePerk(newHorizonsTraining::DIPLOMACY,
			newHorizonsTraining::MERCENARY_CAPTAIN) && stack->getTrainingReceipt().recruitedBy(hero->id, true);
		const int morale = stack->moraleValWithBonus(adjustment + (mercenary ? 1 : 0));
		float multiplier = 1.0f;
		if(morale < 0 && !badMorale.empty())
			multiplier -= 1.0 / dice * badMorale.at(std::min<size_t>(badMorale.size(), -morale) - 1);
		else if(morale > 0 && !goodMorale.empty())
			multiplier += 1.0 / dice * goodMorale.at(std::min<size_t>(goodMorale.size(), morale) - 1);
		const auto power = leadershipAI::stackPower(hero, stack->getCreatureID(), stack->getCount(), &army);
		value = leadershipAI::addPower(value, leadershipAI::scalePower(power, multiplier));
		const auto row = std::ranges::find_if(slots, [&stack](const SlotInfo & candidate)
			{ return candidate.creature->getId() == stack->getCreatureID() && candidate.count == stack->getCount(); });
		if(row != slots.end()) row->power = power;
	}
	return value;
}

uint64_t ArmyManager::leadershipArmyValue(std::vector<SlotInfo> & slots, const CGHeroInstance * hero) const
{
	CCreatureSet projected;
	for(const auto & slot : slots)
		if(!projected.setCreature(projected.getFreeSlot(), slot.creature->getId(), slot.count))
			throw std::invalid_argument("Invalid projected army in Leadership valuation");
	uint64_t value = 0;
	for(auto & slot : slots)
	{
		slot.power = leadershipAI::stackPower(hero, slot.creature->getId(), slot.count, &projected);
		value = leadershipAI::addPower(value, slot.power);
	}
	return value;
}
}
