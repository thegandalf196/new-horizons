/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "BattleSearchPolicy.h"
#include <cassert>
#include "SearchIncumbent.h"
#include "MovementQueryCache.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "ForecastHeroActionRouting.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"

std::shared_ptr<HypotheticBattle> BattleEvaluator::createHypotheticBattle(
	const HypotheticBattle::Subject & source,
	std::shared_ptr<battleSearch::SearchBudget> explicitWork) const
{
	const auto owner = explicitWork ? std::move(explicitWork) : workBudget;
	assert(!workLimits || owner);
	// The evaluator is decision-local. Only its exact still-open native source
	// reuses this owned base; altered/hypothetical input retains its own state.
	const bool reusable = decisionBaseOpen && decisionBase && source == decisionNativeSource
		&& source->battleGetRound() == decisionBase->battleGetRound()
		&& source->getBattle()->getActivationSerial() == decisionBase->getActivationSerial()
		&& source->getBattle()->getCapturedMagicRules() == decisionBase->getCapturedMagicRules();
	const auto & input = reusable ? decisionBase : source;
	auto result = std::make_shared<HypotheticBattle>(env.get(), input,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>(), owner);
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(input.get()))
		forecast::NativeAdapter::forkExchange(*parent, *result);
	if(!result->forecastWorkSink() && decisionReadWork && !result->ownedForecastSize())
		result->setForecastWorkSink(decisionReadWork);
	result->setMovementQueryCache(movementMemo);
	assert(!workLimits || result->searchBudget());
	return result;
}

std::shared_ptr<HypotheticBattle> BattleEvaluator::createHeroCandidateBattle(
	const HypotheticBattle::Subject & source, const PossibleSpellcast & candidate,
	std::shared_ptr<battleSearch::SearchBudget> explicitWork) const
{
	const auto owner = explicitWork ? std::move(explicitWork) : workBudget;
	const bool reusable = decisionBaseOpen && decisionBase && source == decisionNativeSource
		&& source->battleGetRound() == decisionBase->battleGetRound()
		&& source->getBattle()->getActivationSerial() == decisionBase->getActivationSerial()
		&& source->getBattle()->getCapturedMagicRules() == decisionBase->getCapturedMagicRules();
	const HypotheticBattle::Subject current = reusable ? decisionBase : source;
	const auto * admitted = dynamic_cast<const HypotheticBattle *>(current.get());
	auto route = forecast::heroAction::Route::CurrentNativeReference;
	if(admitted && admitted->forecastState())
	{
		if(owner) owner->require(battleSearch::WorkKind::Node,
			static_cast<uint32_t>(admitted->forecastState()->ids().size() + 1));
		route = forecast::heroAction::classify(*current, side, candidate).route;
	}
	// Only an exact unmodified real decision source may bypass the compact base.
	// A reference descendant keeps its modified CURRENT source verbatim.
	const auto & input = route == forecast::heroAction::Route::Compact ? current : source;
	auto result = forecast::heroAction::makeBranch(env.get(), input, owner, route);
	if(!result->forecastWorkSink() && decisionReadWork && !result->ownedForecastSize())
		result->setForecastWorkSink(decisionReadWork);
	result->setMovementQueryCache(movementMemo);
	return result;
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount,
	std::shared_ptr<MovementQueryCache> movementCache
#ifdef NH_PERF_EXPERIMENTS
	, std::shared_ptr<battleSearch::SearchBudget> diagnosticRoot,
	std::shared_ptr<forecast::NativeReadWork> diagnosticReads
#endif
)
	: workLimits(battleSearch::workLimits()),
	workBudget(
#ifdef NH_PERF_EXPERIMENTS
		diagnosticRoot ? std::move(diagnosticRoot) :
#endif
		workLimits ? std::make_shared<battleSearch::SearchBudget>(workLimits->decision) : nullptr),
	movementMemo(movementCache ? std::move(movementCache) : std::make_shared<MovementQueryCache>()),
	scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cb(cb), env(env), playerID(playerID), battleID(battleID), side(side),
	strengthRatio(strengthRatio), simulationTurnsCount(simulationTurnsCount)
{
	assert(!workLimits || workBudget);
#ifdef NH_PERF_EXPERIMENTS
	decisionReadWork = diagnosticReads ? std::move(diagnosticReads) : std::make_shared<forecast::NativeReadWork>();
#endif
	decisionNativeSource = cb->getBattle(battleID);
	decisionBase = createHypotheticBattle(decisionNativeSource);
	// Prepare source definitions/raw programmes once for this real decision.
	// Unsupported roots keep the exact native route and do not reuse a model.
	if(!forecast::NativeAdapter::prepareExchange(*decisionBase, &decisionPreparation))
		decisionBase.reset();
	hb = createHypotheticBattle(decisionNativeSource);
	damageCache.buildDamageCache(hb, side);
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	: workLimits(battleSearch::workLimits()),
	workBudget(hb->searchBudget() ? hb->searchBudget() : workLimits
		? std::make_shared<battleSearch::SearchBudget>(workLimits->decision) : nullptr),
	movementMemo(hb->movementQueryCache() ? hb->movementQueryCache() : std::make_shared<MovementQueryCache>()),
	hb(hb), scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cb(cb), env(env), playerID(playerID), battleID(battleID), side(side),
	damageCache(damageCache), strengthRatio(strengthRatio), simulationTurnsCount(simulationTurnsCount)
{
	decisionBaseOpen = false;
	hb->setMovementQueryCache(movementMemo);
	hb->setSearchBudget(workBudget);
	assert(!workLimits || hb->searchBudget());
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleAction BattleEvaluator::legalSearchIncumbent(const CStack * stack) const
{
	return battleSearch::preparedIncumbent(stack, *targets);
}
