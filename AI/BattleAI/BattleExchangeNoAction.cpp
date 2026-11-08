/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"

float BattleExchangeEvaluator::evaluateNoActionContinuation(const battle::Unit * actor,
	DamageCache & cache, const std::shared_ptr<HypotheticBattle> & state) const
{
	if(!actor || state->battleGetUnitByID(actor->unitId()) != actor
		|| !state->wasCurrentActivationForfeited(actor->unitId())
		|| !state->currentForfeitCompleted(actor->unitId()))
		throw std::invalid_argument("No-action exchange requires its completed typed activation forfeiture");
	// A fixed self/self metadata anchor has no chosen enemy, attack/proc evaluation,
	// movement or selectable action. Full current membership preserves the existing
	// remaining-unit exchange and ordinary future rounds at the unchanged horizon.
	BattleAttackInfo metadata(actor, actor, 0, false);
	AttackPossibility anchor(actor->getPosition(), actor->getPosition(), metadata);
	anchor.noActionAnchor = true;
	PotentialTargets none(actor, cache, state);
	const auto score = calculateExchange(anchor, 0, none, cache, state, state->battleGetAllUnits(false));
	return scoreValue(score);
}

bool BattleExchangeEvaluator::acceptsExchangeAnchor(const AttackPossibility & ap, const HypotheticBattle & state) const
{
	if(ap.noActionAnchor)
	{
		const auto * actor = ap.attack.attacker;
		if(!actor || ap.attack.defender != actor || state.battleGetUnitByID(actor->unitId()) != actor
			|| state.battleActiveUnit() != actor || ap.from != actor->getPosition() || ap.dest != actor->getPosition()
			|| !state.wasCurrentActivationForfeited(actor->unitId()) || !state.currentForfeitCompleted(actor->unitId()))
			throw std::invalid_argument("No-action anchor is not its completed actual activation");
		return true;
	}
	return !(cb->battleGetMySide() == BattleSide::LEFT_SIDE
		&& cb->battleGetGateState() == EGateState::BLOCKED
		&& ap.attack.defender->coversPos(BattleHex::GATE_BRIDGE));
}

ReachabilityData BattleExchangeEvaluator::noActionMembership(const HypotheticBattle & state) const
{
	ReachabilityData result;
	// Every remaining participant is included independently of anchor geometry.
	// Existing initiative order and horizon determine when each can act.
	result.melleeAccessible = state.battleGetAllUnits(false);
	for(size_t turn = 0; turn < turnOrder.size(); ++turn)
		for(const auto * source : turnOrder[turn])
			if(const auto * unit = state.battleGetUnitByID(source->unitId()); unit && unit->alive())
				result.units[turn].push_back(unit);
	return result;
}
