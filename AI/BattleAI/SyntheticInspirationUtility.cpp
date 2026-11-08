/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "HeroActionContinuation.h"
#include "PossibleSpellcast.h"
#include "HeroSpellOutcomeValue.h"
#include "ElementalAttunementTerminalValue.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <cmath>
float BattleEvaluator::conditionalInspirationUtility(
	const std::shared_ptr<HypotheticBattle> & accepted, uint32_t actorId, bool full)
{
	const auto & receipts = accepted->getArcaneInspirationEffects();
	if(receipts.empty()) return 0.0f;
	const auto evaluate = [&](bool withoutInspiration)
	{
		auto branch = std::make_shared<HypotheticBattle>(env.get(), accepted);
		if(withoutInspiration)
			branch->reverseArcaneInspirationEffects(receipts);
		auto actor = branch->getForUpdate(actorId);
		if(cachedAttack.waited) branch->makeWait(actor.get());
		DamageCache safeCopy = damageCache;
		DamageCache inner(&safeCopy);
		// Existing mutation/signature-aware lazy cache; no eager all-roster rebuild per outcome.
		heroActionUtility::AbsoluteContinuationUtility result{0};
		heroActionUtility::evaluateContinuation(result, actor, full || accepted->hasObstacleChanges() || accepted->hasWallChanges(), cachedAttack, inner,
			branch, env, strengthRatio, simulationTurnsCount, scoreEvaluator, targets.get());
		return result.value;
	};
	// Evaluate the SAME accepted candidate and existing continuation twice,
	// reversing only the ordered canonical completion receipts. Other heuristic effects stay in
	// both branches under the existing conditional castEval convention; no
	// root-wide cached marginal, new resistance outcomes or extra tactical depth.
	const auto with = evaluate(false), without = evaluate(true);
	if(std::isfinite(with) && std::isfinite(without)
		&& with > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
		&& without > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2))
		return with - without;
	return 0.0f;
}

void BattleEvaluator::includeSyntheticInspiration(PossibleSpellcast & candidate,
	const std::shared_ptr<HypotheticBattle> & accepted, uint32_t actorId)
{
	if(!std::isfinite(candidate.value) || candidate.value == std::numeric_limits<float>::lowest()) return;
	// Terminal spell heuristics omit the generic post-cast exchange. Credit only
	// their newly activated elemental family, using the existing creature kernel.
	candidate.value += elementalAttunementAI::terminalMarginal(accepted, side,
		hb->getElementalAttunementMask(side), damageCache,
		scoreEvaluator.getPositiveEffectMultiplier(), scoreEvaluator.getNegativeEffectMultiplier());
	if(candidate.spellInspirationExpectedValue) candidate.value += *candidate.spellInspirationExpectedValue;
	else candidate.value += conditionalInspirationUtility(accepted, actorId, false);
}

float heroSpellProjection::localReceiptMarginal(const std::shared_ptr<HypotheticBattle> & board,
	const OutcomeKernel & kernel)
{
	if(board->getArcaneInspirationEffects().empty()) return 0.0f;
	auto without = std::make_shared<HypotheticBattle>(board->env, board);
	without->reverseArcaneInspirationEffects(board->getArcaneInspirationEffects());
	return kernel(board) - kernel(without);
}
