/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "TemporaryGatingProjection.h"
#include "../../lib/CStack.h"
std::optional<BattleAction> BattleEvaluator::chooseTemporaryGate(const CStack * source,
	const BattleAction & ordinary)
{
	if(!source || !newHorizonsDemonicGating::eligibleOriginalStack(*hb, *source, side)) return std::nullopt;
	// The selected active ability has its own prepared value/effects. Do not
	// replace it using a physical-attack opportunity-cost estimate.
	if(ordinary.actionType != EActionType::WALK && ordinary.actionType != EActionType::WAIT
		&& ordinary.actionType != EActionType::DEFEND && ordinary.actionType != EActionType::WALK_AND_ATTACK
		&& ordinary.actionType != EActionType::SHOOT) return std::nullopt;
	const float opportunity = cachedAttack.ap ? std::max(0.0f, cachedAttack.ap->attackValue()) : 0;
	return temporaryGatingAI::projectedChoice(env.get(), hb, *source, side, damageCache,
		opportunity, simulationTurnsCount);
}
