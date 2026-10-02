/*
 * NewHorizonsMuster.cpp, part of VCMI engine
 */
#include "StdInc.h"
#include "NewHorizonsMuster.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <ranges>
#include <vector>

namespace NK2AI::newHorizonsMuster
{

namespace
{
	int leadershipAdmittedAmount(const CGHeroInstance * hero, const CreatureID creature, const int amount)
	{
		if(amount <= 0)
			return 0;
		if(!hero)
			return amount;

		const auto capacity = hero->getLeadershipSlotCapacity(creature);
		if(!capacity)
			return amount;

		const auto slot = hero->getSlotFor(creature);
		const int currentCount = slot.validSlot() ? hero->getStackCount(slot) : 0;
		return std::min(amount, std::max(0, capacity->maximum - currentCount));
	}
}

std::optional<int> amountMultiplier(const int recruitmentRank,
	const newHorizonsCreatures::CreatureCategory category,
	const ::newHorizonsMuster::PerkModifiers & modifiers)
{
	return ::newHorizonsMuster::amountForCategory(recruitmentRank, category, modifiers);
}

std::optional<Candidate> chooseBroadMusterSplit(const std::vector<CoreMusterRow> & coreRows,
	const int totalAmount, const bool broadMusterActive, const int64_t bestSoloRecruitableArmyValue)
{
	if(!broadMusterActive || totalAmount <= 1 || coreRows.size() < 2)
		return std::nullopt;

	// Visit candidates in stable town-row order, so the first equal-valued split
	// remains deterministic and cannot depend on creature/entity ID ordering.
	auto rows = coreRows;
	std::ranges::stable_sort(rows, [](const CoreMusterRow & lhs, const CoreMusterRow & rhs)
	{
		return lhs.row < rhs.row;
	});

	std::optional<Candidate> bestSplit;
	for(size_t firstIndex = 0; firstIndex < rows.size(); ++firstIndex)
	{
		const auto & first = rows[firstIndex];
		if(first.creature == CreatureID::NONE || first.row < 0 || first.aiValue < 0
			|| first.currentStackCount < 0 || (first.leadershipMaximum && *first.leadershipMaximum < 0))
			continue;

		for(size_t secondIndex = firstIndex + 1; secondIndex < rows.size(); ++secondIndex)
		{
			const auto & second = rows[secondIndex];
			if(second.creature == CreatureID::NONE || second.row < 0 || second.aiValue < 0
				|| second.currentStackCount < 0 || (second.leadershipMaximum && *second.leadershipMaximum < 0)
				|| first.row == second.row || first.creature == second.creature)
				continue;

			for(int firstAmount = 1; firstAmount < totalAmount; ++firstAmount)
			{
				const int secondAmount = totalAmount - firstAmount;
				const auto admitted = [](const CoreMusterRow & row, const int count)
				{
					if(!row.leadershipMaximum)
						return count;
					return std::min(count, std::max(0, *row.leadershipMaximum - row.currentStackCount));
				};
				const int admittedFirst = admitted(first, firstAmount);
				const int admittedSecond = admitted(second, secondAmount);
				const int64_t recruitableValue = static_cast<int64_t>(first.aiValue) * admittedFirst
					+ static_cast<int64_t>(second.aiValue) * admittedSecond;
				if(recruitableValue <= bestSoloRecruitableArmyValue
					|| (bestSplit && recruitableValue <= bestSplit->recruitableArmyValue))
					continue;

				Candidate candidate;
				candidate.creature = first.creature;
				candidate.secondCreature = second.creature;
				candidate.category = newHorizonsCreatures::CreatureCategory::CORE;
				candidate.amount = totalAmount;
				candidate.firstAmount = firstAmount;
				candidate.secondAmount = secondAmount;
				candidate.armyValue = static_cast<int64_t>(first.aiValue) * firstAmount
					+ static_cast<int64_t>(second.aiValue) * secondAmount;
				candidate.recruitableArmyValue = recruitableValue;
				candidate.row = first.row;
				candidate.secondRow = second.row;
				if(candidate.valid())
					bestSplit = candidate;
			}
		}
	}

	return bestSplit;
}

std::optional<Candidate> chooseTownCandidate(const CGDwelling & town,
	const IGameInfoCallback & callback,
	const int recruitmentRank,
	const ::newHorizonsMuster::PerkModifiers & modifiers,
	const CGHeroInstance * hero,
	const bool broadMusterActive)
{
	std::optional<Candidate> best;
	std::vector<CoreMusterRow> coreRows;
	int64_t bestSoloRecruitableArmyValue = 0;

	for(size_t row = 0; row < town.creatures.size(); ++row)
	{
		const auto & [available, choices] = town.creatures[row];
		static_cast<void>(available); // Empty pools are valid Muster targets.
		if(choices.empty())
			continue;

		// A dwelling row can expose a base creature and one or more upgraded
		// forms.  Match normal VCMI recruitment and target the currently best
		// available form, which is represented by the final row choice.
		const CreatureID creature = choices.back();
		const auto category = callback.getCreatureCategory(creature);
		if(!category)
			continue; // legacy/no-category worlds are never New Horizons targets

		const auto amount = amountMultiplier(recruitmentRank, category->category, modifiers);
		if(!amount)
			continue;

		const auto * creatureType = creature.toCreature();
		if(!creatureType)
			continue;

		Candidate candidate;
		candidate.creature = creature;
		candidate.category = category->category;
		candidate.amount = *amount;
		candidate.armyValue = static_cast<int64_t>(creatureType->getAIValue()) * *amount;
		candidate.recruitableArmyValue = static_cast<int64_t>(creatureType->getAIValue())
			* leadershipAdmittedAmount(hero, creature, *amount);
		candidate.row = static_cast<int>(row);
		bestSoloRecruitableArmyValue = std::max(bestSoloRecruitableArmyValue, candidate.recruitableArmyValue);

		if(category->category == newHorizonsCreatures::CreatureCategory::CORE)
		{
			CoreMusterRow coreRow;
			coreRow.creature = creature;
			coreRow.row = static_cast<int>(row);
			coreRow.aiValue = creatureType->getAIValue();
			coreRow.currentStackCount = 0;
			if(hero)
			{
				const auto slot = hero->getSlotFor(creature);
				if(slot.validSlot())
					coreRow.currentStackCount = hero->getStackCount(slot);
				if(const auto capacity = hero->getLeadershipSlotCapacity(creature))
					coreRow.leadershipMaximum = capacity->maximum;
			}
			coreRows.push_back(coreRow);
		}

		// Keep row order as the final tie breaker.  This is deterministic and
		// avoids a different choice merely because map/entity IDs happened to
		// be allocated differently in a test fixture.
		if(!best || candidate.armyValue > best->armyValue
			|| (candidate.armyValue == best->armyValue && candidate.row < best->row))
			best = candidate;
	}

	if(broadMusterActive)
	{
		const auto coreAmount = amountMultiplier(recruitmentRank,
			newHorizonsCreatures::CreatureCategory::CORE, modifiers);
		if(coreAmount)
		{
			if(auto split = chooseBroadMusterSplit(coreRows, *coreAmount, true, bestSoloRecruitableArmyValue))
				return split;
		}
	}

	return best;
}

std::optional<Candidate> chooseExternalCandidate(const CGDwelling & dwelling,
	const IGameInfoCallback & callback,
	const int recruitmentRank,
	const bool externalRecruiterActive)
{
	if(dwelling.ID != Obj::CREATURE_GENERATOR1 && dwelling.ID != Obj::CREATURE_GENERATOR4)
		return std::nullopt;

	std::optional<Candidate> best;
	for(size_t row = 0; row < dwelling.creatures.size(); ++row)
	{
		const auto & [available, choices] = dwelling.creatures[row];
		static_cast<void>(available); // Empty pools are valid Muster targets.
		if(choices.empty())
			continue;

		const CreatureID creature = choices.back();
		const auto category = callback.getCreatureCategory(creature);
		if(!category)
			continue;

		const auto amount = externalAmountMultiplier(recruitmentRank, category->category,
			externalRecruiterActive);
		if(!amount)
			continue;

		const auto * creatureType = creature.toCreature();
		if(!creatureType)
			continue;

		Candidate candidate;
		candidate.creature = creature;
		candidate.category = category->category;
		candidate.amount = *amount;
		candidate.armyValue = static_cast<int64_t>(creatureType->getAIValue()) * *amount;
		candidate.row = static_cast<int>(row);

		if(!best || candidate.armyValue > best->armyValue
			|| (candidate.armyValue == best->armyValue && candidate.row < best->row))
			best = candidate;
	}

	return best;
}

} // namespace NK2AI::newHorizonsMuster
