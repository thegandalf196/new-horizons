/*
 * HeroActionContinuation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionContinuation.h"
#include "BattleEvaluator.h"
#include "../../lib/battle/BattleAttackInfo.h"

namespace heroActionUtility
{
void evaluateContinuation(AbsoluteContinuationUtility & continuation,
	const std::shared_ptr<StackWithBonuses> & modelActive, bool needFullEval,
	const CachedAttack & cachedAttack, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const std::shared_ptr<Environment> & env,
	float strengthRatio, int simulationTurnsCount,
	const BattleExchangeEvaluator & scoreEvaluator, PotentialTargets * targets)
{
	auto & stackActionScore = continuation.value;
	if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
	{
#if BATTLE_TRACE_LEVEL >= 1
		logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

		PotentialTargets innerTargets(modelActive.get(), innerCache, state);
		BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

		innerEvaluator.updateReachabilityMap(state);

		auto moveTarget = innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state);

		if(!innerTargets.possibleAttacks.empty())
		{
			auto newStackAction = innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state);

			stackActionScore = std::max(moveTarget.score, newStackAction.score);
		}
		else
		{
			stackActionScore = moveTarget.score;
		}
	}
	else if(modelActive->alive())
	{
		auto updatedAttacker = state->getForUpdate(cachedAttack.ap->attack.attacker->unitId());
		auto updatedDefender = state->getForUpdate(cachedAttack.ap->attack.defender->unitId());
		auto updatedBai = BattleAttackInfo(
			updatedAttacker.get(),
			updatedDefender.get(),
			cachedAttack.ap->attack.chargeDistance,
			cachedAttack.ap->attack.shooting);

		auto updatedAttack = AttackPossibility::evaluate(updatedBai, cachedAttack.ap->from, innerCache, state, cachedAttack.ap->perfectMoment);

		BattleExchangeEvaluator innerEvaluator(scoreEvaluator);

		stackActionScore = innerEvaluator.evaluateExchange(updatedAttack, cachedAttack.turn, *targets, innerCache, state);
	}
}
}
