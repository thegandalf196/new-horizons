/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "SelectionStageTiming.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/NewHorizonsArchery.h"
using battleSelectionTiming::measureSelectionStage;

BattleAction BattleEvaluator::evaluatedAttackAction(const CStack * stack, const EvaluationResult & evaluationResult,
	float score, bool hasEnemyMelee, SelectionTiming * timing)
{
	const auto & bestAttack = evaluationResult.bestAttack;

	if(evaluationResult.wait && !stack->acquireState()->waitedThisTurn)
	{
		return BattleAction::makeWait(stack);
	}
	else if(bestAttack.attack.shooting)
	{
		activeActionMade = true;
		const auto * hero = hb->battleGetFightingHero(stack->unitSide());
		if(bestAttack.from.isValid() && bestAttack.from != stack->getPosition()
			&& newHorizonsArchery::canUseSkirmisher(hero, stack)
			&& hb->battleCanSkirmisherAttackFromHex(stack,
				bestAttack.attack.defender->getPosition(), bestAttack.from))
		{
			// Skirmisher encodes its chosen legal move-and-fire destination through
			// the existing WALK_AND_ATTACK packet.
			auto action = BattleAction::makeMeleeAttack(stack,
				bestAttack.attack.defender->getPosition(), bestAttack.from, false);
			action.archerySkirmisherAttack = true;
			action.perfectMoment = bestAttack.perfectMoment;
			return action;
		}
		auto action = BattleAction::makeShotAttack(stack, bestAttack.attack.defender);
		action.perfectMoment = bestAttack.perfectMoment;
		return action;
	}
	else
	{
		if(bestAttack.collateralDamageReduce
			&& bestAttack.collateralDamageReduce >= bestAttack.defenderDamageReduce / 2
			&& score < 0)
		{
			return BattleAction::makeDefend(stack);
		}

		bool isTargetOutsideFort = !hb->battleIsInsideWalls(bestAttack.from);
		bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
			&& !bestAttack.attack.shooting
			&& hasWorkingTowers()
			&& hasEnemyMelee
			&& isTargetOutsideFort;

		if(siegeDefense)
		{
			logAi->trace("Evaluating exchange at %d self-defense", stack->getPosition());

			BattleAttackInfo bai(stack, stack, 0, false);
			AttackPossibility apDefend(stack->getPosition(), stack->getPosition(), bai);

			float defenseValue = measureSelectionStage(timing ? &timing->siegeDefense : nullptr, [&]()
			{
				return scoreEvaluator.evaluateExchange(apDefend, 0, *targets, damageCache, hb);
			}, timing ? &timing->targetExchange : nullptr);

			if((defenseValue > score && score <= 0) || (defenseValue > 2 * score && score > 0))
			{
				return BattleAction::makeDefend(stack);
			}
		}

		activeActionMade = true;
		auto action = BattleAction::makeMeleeAttack(stack, bestAttack.attack.defenderPos, bestAttack.from);
		action.perfectMoment = bestAttack.perfectMoment;
		return action;
	}

}
