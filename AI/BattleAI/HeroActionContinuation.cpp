/*
 * HeroActionContinuation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionContinuation.h"
#include "HeroActionTiming.h"
#include "BattleEvaluator.h"
#include "../../lib/battle/BattleAttackInfo.h"

namespace heroActionUtility
{
void evaluateContinuation(AbsoluteContinuationUtility & continuation,
	const std::shared_ptr<StackWithBonuses> & modelActive, bool needFullEval,
	const CachedAttack & cachedAttack, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const std::shared_ptr<Environment> & env,
	float strengthRatio, int simulationTurnsCount,
	const BattleExchangeEvaluator & scoreEvaluator, PotentialTargets * targets,
	heroActionTiming::Continuation * timing)
{
	auto & stackActionScore = continuation.value;
	if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
	{
		heroActionTiming::Scope measurement(timing ? &timing->full : nullptr);
#if BATTLE_TRACE_LEVEL >= 1
		logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

		auto innerTargets = [&]
		{
			heroActionTiming::Scope setup(timing ? &timing->fullSetup : nullptr);
			return PotentialTargets(modelActive.get(), innerCache, state);
		}();
		BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

		{
			heroActionTiming::Scope reachability(timing ? &timing->fullReachability : nullptr);
			innerEvaluator.updateReachabilityMap(state);
		}

		auto moveTarget = [&]
		{
			heroActionTiming::Scope movement(timing ? &timing->fullMovement : nullptr);
			return innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state,
				timing ? &timing->fullMovementDetails : nullptr);
		}();

		if(!innerTargets.possibleAttacks.empty())
		{
			auto newStackAction = [&]
			{
				heroActionTiming::Scope bestTarget(timing ? &timing->fullBestTarget : nullptr);
				return innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state, false,
					timing ? &timing->fullBestTargetDetails : nullptr);
			}();

			stackActionScore = std::max(moveTarget.score, newStackAction.score);
		}
		else
		{
			stackActionScore = moveTarget.score;
		}
	}
	else if(modelActive->alive())
	{
		heroActionTiming::Scope measurement(timing ? &timing->cached : nullptr);
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
