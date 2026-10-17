/*
 * HeroActionContinuation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HeroActionUtility.h"

class Environment;
class PotentialTargets;
class StackWithBonuses;
struct CachedAttack;

namespace heroActionUtility
{
/// Caller retains modelActive and its original branch-local lifetime. Preserve
/// full-search/cached-exchange branching, including lazy *targets access. This
/// uses the EXISTING exchange horizon, not a new complete Order forecast.
void evaluateContinuation(AbsoluteContinuationUtility & continuation,
	const std::shared_ptr<StackWithBonuses> & modelActive, bool needFullEval,
	const CachedAttack & cachedAttack, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const std::shared_ptr<Environment> & env,
	float strengthRatio, int simulationTurnsCount,
	const BattleExchangeEvaluator & scoreEvaluator, PotentialTargets * targets);
}
