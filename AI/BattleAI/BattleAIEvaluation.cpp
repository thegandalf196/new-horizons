/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleAI.h"
#include "BattleEvaluator.h"
#ifdef NH_PERF_EXPERIMENTS
#include "BattleAITiming.h"
#endif
#include "BattleStrengthRatio.h"
#include "HeroDecisionPressure.h"
#include "BattleAIDiagnosticAdmission.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include <memory>

float battleAI::strengthRatio(const CBattleInfoCallback & battle, BattleSide side)
{
	auto stacks = battle.battleGetAllStacks();
	long double our = 0;
	long double enemy = 0;

	for(auto stack : stacks)
	{
		auto creature = stack->creatureId().toCreature();

		if(!creature)
			continue;

		// Both an individual product and the roster sum can exceed signed
		// integer ranges. Widen before multiplying, then retain the original
		// float numerator/denominator rounding at the final division boundary.
		const auto strength = static_cast<long double>(stack->getCount()) * creature->getAIValue();
		if(stack->unitSide() == side)
			our += strength;
		else
			enemy += strength;
	}

	return enemy == 0 ? 1.0f : static_cast<float>(our) / static_cast<float>(enemy);
}

int getSimulationTurnsCount(const StartInfo * startInfo)
{
	return startInfo->difficulty < 4 ? 2 : 10;
}

BattleEvaluator CBattleAI::makeEvaluator(const BattleID & battleID, const CStack * stack
#ifdef NH_PERF_EXPERIMENTS
	, TimingScope * diagnostics
#endif
)
{
#ifdef NH_PERF_EXPERIMENTS
	const auto diagnosticRoot = diagnostics ? diagnostics->beginSearchDecision(searchProfile) : nullptr;
#endif
	return BattleEvaluator(env, cb, stack, playerID, battleID, side,
		battleAI::strengthRatio(*cb->getBattle(battleID), side), getSimulationTurnsCount(env->game()->getStartInfo())
#ifdef NH_PERF_EXPERIMENTS
		, {}, diagnosticRoot, diagnostics ? diagnostics->forecastWorkSink() : nullptr
#endif
	);
}

bool CBattleAI::attemptHeroAction(BattleEvaluator & evaluator, const CStack * stack)
{
	heroDecisionPressure::Experiment shortlist;

	if(!diagnosticAdmissionMask)
		return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage,
			heroValueObserver, vinesReuseControl, nullptr, nullptr, &shortlist);
	battleAIDiagnostic::Counts counts;
	battleAIDiagnostic::Admission admission(*diagnosticAdmissionMask, counts);
	const bool submitted = evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage,
		heroValueObserver, vinesReuseControl, nullptr, nullptr, &shortlist, nullptr, &admission);
	if(diagnosticAdmissionObserver) diagnosticAdmissionObserver->completedHeroAdmission(counts);
	return submitted;
}
