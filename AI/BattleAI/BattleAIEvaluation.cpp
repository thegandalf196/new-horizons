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
#include "HeroDecisionPressure.h"
#include "BattleAIDiagnosticAdmission.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include <memory>

static float getStrengthRatio(std::shared_ptr<CBattleInfoCallback> cb, BattleSide side)
{
	auto stacks = cb->battleGetAllStacks();
	auto our = 0;
	auto enemy = 0;

	for(auto stack : stacks)
	{
		auto creature = stack->creatureId().toCreature();

		if(!creature)
			continue;

		if(stack->unitSide() == side)
			our += stack->getCount() * creature->getAIValue();
		else
			enemy += stack->getCount() * creature->getAIValue();
	}

	return enemy == 0 ? 1.0f : static_cast<float>(our) / enemy;
}

int getSimulationTurnsCount(const StartInfo * startInfo)
{
	return startInfo->difficulty < 4 ? 2 : 10;
}

BattleEvaluator CBattleAI::makeEvaluator(const BattleID & battleID, const CStack * stack)
{
	return BattleEvaluator(env, cb, stack, playerID, battleID, side,
		getStrengthRatio(cb->getBattle(battleID), side), getSimulationTurnsCount(env->game()->getStartInfo()));
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
