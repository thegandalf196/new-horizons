/*
 * SummonEvaluation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "SummonEvaluation.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

using namespace spells;

namespace summonEvaluation
{
bool usesSelectedPlacement(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && (spell->getJsonKey() == "new-horizons:summonTrolls"
		|| spell->getJsonKey() == "new-horizons:elementalConvergence"
		|| spell->getJsonKey() == "new-horizons:summonLivingArmor");
}

std::vector<Target> placementTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!usesSelectedPlacement(spellMechanics) || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::LOCATION})
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable())
			continue;

		Target target{Destination(hex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	// Exact creation HP is independent of the chosen hex. BattleEvaluator still
	// projects each placement through its ordinary combat continuation. Sample
	// the ordered legal set across the whole field so that turn cost stays bounded
	// without preferring one edge of the battlefield.
	constexpr size_t MAX_PLACEMENT_CANDIDATES = 12;
	if(result.size() <= MAX_PLACEMENT_CANDIDATES)
		return result;

	std::vector<Target> sampled;
	sampled.reserve(MAX_PLACEMENT_CANDIDATES);
	for(size_t index = 0; index < MAX_PLACEMENT_CANDIDATES; ++index)
	{
		const auto sourceIndex = index * (result.size() - 1) / (MAX_PLACEMENT_CANDIDATES - 1);
		sampled.push_back(std::move(result[sourceIndex]));
	}
	return sampled;
}

float createdStackValue(const CSpell * spell, const battle::Unit * unit,
	bool convergenceElemental)
{
	if(!unit || !unit->unitType() || !unit->isSummoned() || unit->getAvailableHealth() <= 0)
		return 0.0f;
	const auto state = unit->acquireState();
	if(!state)
		return 0.0f;
	const bool nature = state->natureSummoned && (convergenceElemental
		|| (spell && spell->getJsonKey() == "new-horizons:summonTrolls"
			&& unit->unitType()->getJsonKey() == "core:troll"));
	const bool armor = spell && spell->getJsonKey() == "new-horizons:summonLivingArmor"
		&& unit->unitType()->getJsonKey() == "new-horizons:livingArmor"
		&& state->summonRoundsRemaining > 0;
	if(!nature && !armor)
		return 0.0f;

	// Magical summons bypass the ordinary health delta. Preserve the native
	// template value and exact wounded final creature; physical and magical
	// defenses flow through the ordinary continuation's real combat queries.
	const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
	return static_cast<float>(unit->getAvailableHealth())
		* static_cast<float>(unit->unitType()->getAIValue())
		/ static_cast<float>(maxHealth);
}
}
