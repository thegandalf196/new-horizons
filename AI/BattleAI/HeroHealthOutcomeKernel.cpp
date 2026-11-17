/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroSpellOutcomeValue.h"
#include "HypotheticBattle.h"
#include "AttackPossibility.h"
#include "SearchBudget.h"
heroSpellProjection::OutcomeKernel heroSpellProjection::healthOutcomeKernel(PlayerColor scoringPlayer, battleSearch::SearchBudget * budget)
{
	return [scoringPlayer, budget](const std::shared_ptr<HypotheticBattle> & board)
	{
		DamageCache local; float value = 0.0f;
		for(const auto * unit : board->battleGetAllUnits(false))
			{
				battleSearch::require(budget, battleSearch::WorkKind::Node);
				if(unit && unit->alive() && !unit->isGhost() && !unit->isTurret())
			{
				const auto potential = AttackPossibility::calculateDamageReduce(nullptr, unit,
					unit->getAvailableHealth(), local, board);
				value += board->battleGetOwner(unit) == scoringPlayer ? potential : -potential;
			}
			}
		return value;
	};
}
