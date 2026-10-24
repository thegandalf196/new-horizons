/* ProjectedBestAction.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "ProjectedBestAction.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusList.h"
void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action)
{
	auto attackerState = state.getForUpdate(liveUnit->unitId());
	const bool attackerWasAlive = attackerState->alive();
	*attackerState = *action.attackerState;
	state.recordBloodrageTransition(attackerState, attackerWasAlive);

	if(action.defenderDamageReduce > 0)
	{
		attackerState->removeUnitBonus(Bonus::UntilAttack);
		attackerState->removeUnitBonus(Bonus::UntilOwnAttack);
	}
	if(action.attackerDamageReduce > 0)
		attackerState->removeUnitBonus(Bonus::UntilBeingAttacked);

	// Accepted movement/attack actions break Sanctuary before resolving. Keep
	// the detached actor used by later morale forecasts in the same state.
	const auto sanctuaryMarkers = attackerState->getBonusesOfType(BonusType::SANCTIFIED);
	if(sanctuaryMarkers && !sanctuaryMarkers->empty())
	{
		std::vector<BonusSourceID> sanctuarySources;
		for(const auto & marker : *sanctuaryMarkers)
		{
			if(!marker || marker->source != BonusSource::SPELL_EFFECT)
				continue;
			if(std::find(sanctuarySources.begin(), sanctuarySources.end(), marker->sid) == sanctuarySources.end())
				sanctuarySources.push_back(marker->sid);
		}
		for(const auto & sourceID : sanctuarySources)
			attackerState->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT, sourceID));
		attackerState->removeUnitBonus(Selector::type()(BonusType::SANCTIFIED));
	}

	for(const auto & affected : action.affectedUnits)
	{
		if(!affected)
			continue;
		auto affectedState = state.getForUpdate(affected->unitId());
		const bool affectedWasAlive = affectedState->alive();
		*affectedState = *affected;
		state.recordBloodrageTransition(affectedState, affectedWasAlive);

		if(action.defenderDamageReduce > 0)
			affectedState->removeUnitBonus(Bonus::UntilBeingAttacked);
		if(action.attackerDamageReduce > 0 && action.attack.defender->unitId() == affected->unitId())
			affectedState->removeUnitBonus(Bonus::UntilAttack);
	}
}
