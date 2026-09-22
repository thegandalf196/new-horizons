/*
 * NewHorizonsMuster.cpp, part of VCMI engine
 */
#include "StdInc.h"
#include "NewHorizonsMuster.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/mapObjects/CGDwelling.h"

namespace NK2AI::newHorizonsMuster
{

std::optional<int> amountMultiplier(const int recruitmentRank,
	const newHorizonsCreatures::CreatureCategory category,
	const ::newHorizonsMuster::PerkModifiers & modifiers)
{
	return ::newHorizonsMuster::amountForCategory(recruitmentRank, category, modifiers);
}

std::optional<Candidate> chooseTownCandidate(const CGDwelling & town,
	const IGameInfoCallback & callback,
	const int recruitmentRank,
	const ::newHorizonsMuster::PerkModifiers & modifiers)
{
	std::optional<Candidate> best;

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
		candidate.row = static_cast<int>(row);

		// Keep row order as the final tie breaker.  This is deterministic and
		// avoids a different choice merely because map/entity IDs happened to
		// be allocated differently in a test fixture.
		if(!best || candidate.armyValue > best->armyValue
			|| (candidate.armyValue == best->armyValue && candidate.row < best->row))
			best = candidate;
	}

	return best;
}

} // namespace NK2AI::newHorizonsMuster
