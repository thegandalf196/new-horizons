/*
 * BattleAITiming.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleAITiming.h"

#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"

#include <cstdlib>
#include <exception>
#include <mutex>
#include <string_view>

namespace
{
bool waitTimingEnabled()
{
	static const bool requested = []
	{
		const char * value = std::getenv("NH_RENDER_LOAD_TIMINGS");
		return value && std::string_view(value) == "1";
	}();
	return requested && logGlobal->isDebugEnabled() && logAi->isDebugEnabled();
}

unsigned int waitTimingLimit()
{
	static const unsigned int limit = []
	{
		const char * value = std::getenv("NH_RENDER_LOAD_TIMING_LIMIT");
		if(!value || !*value)
			return 1024u;
		unsigned int result = 0;
		for(const char * current = value; *current; ++current)
		{
			if(*current < '0' || *current > '9')
				return 1024u;
			const auto digit = static_cast<unsigned int>(*current - '0');
			if(result > (16384u - digit) / 10)
				return 1024u;
			result = result * 10 + digit;
		}
		return result ? result : 1024u;
	}();
	return limit;
}
}

struct CBattleAI::TimingState
{
	using Clock = std::chrono::steady_clock;
	const BattleID battleID;
	const PlayerColor player;
	const Clock::time_point started = Clock::now();
	std::optional<Clock::time_point> ended;
	std::mutex mutex;
	TimingSummary summary;
	int32_t lastObservedRound = -1;
	uint64_t waitTraceAttempts = 0;

	TimingState(BattleID battleID, PlayerColor player) : battleID(battleID), player(player) {}

	TimingSummary snapshotLocked() const
	{
		auto result = summary;
		result.battleWallMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
			ended.value_or(Clock::now()) - started).count();
		return result;
	}

	std::optional<TimingSummary> reportLocked()
	{
		if(!summary.finished || summary.activeStackInFlight != 0 || summary.reported)
			return std::nullopt;
		summary.reported = true;
		return snapshotLocked();
	}

	void log(const std::optional<TimingSummary> & result) const
	{
		if(!result)
			return;
		logAi->info("PERFORMANCE: BattleAI callback wall battle=%d player=%s battle_us=%llu calls=%llu total_us=%llu max_us=%llu rounds_observed=%u"
			" evaluator_construct_calls=%llu evaluator_construct_us=%llu stack_select_calls=%llu stack_select_us=%llu"
			" select_creature_spell_calls=%llu select_creature_spell_us=%llu"
			" select_movement_search_calls=%llu select_movement_search_us=%llu"
			" move_find_calls=%llu move_find_us=%llu move_go_calls=%llu move_go_us=%llu move_remainder_us=%llu"
			" move_find_exchange_subset_calls=%llu move_find_exchange_subset_us=%llu"
			" exchange_body_subset_calls=%llu exchange_body_subset_us=%llu"
			" exchange_membership_calls=%llu exchange_membership_us=%llu"
			" exchange_projection_setup_calls=%llu exchange_projection_setup_us=%llu"
			" exchange_target_probe_calls=%llu exchange_target_probe_us=%llu"
			" exchange_target_probe_preparation_calls=%llu exchange_target_probe_preparation_us=%llu"
			" exchange_target_probe_main_damage_calls=%llu exchange_target_probe_main_damage_us=%llu"
			" exchange_target_probe_dps_score_calls=%llu exchange_target_probe_dps_score_us=%llu"
			" exchange_applied_attack_calls=%llu exchange_applied_attack_us=%llu"
			" exchange_activation_start_calls=%llu exchange_activation_start_us=%llu"
			" exchange_activation_end_calls=%llu exchange_activation_end_us=%llu"
			" exchange_round_advance_calls=%llu exchange_round_advance_us=%llu exchange_remainder_us=%llu"
			" move_go_blocking_subset_calls=%llu move_go_blocking_subset_us=%llu"
			" select_target_exchange_calls=%llu select_target_exchange_us=%llu select_remainder_us=%llu"
			" select_best_target_calls=%llu select_best_target_us=%llu"
			" best_target_waited_exchange_calls=%llu best_target_waited_exchange_us=%llu"
			" best_target_waited_body_calls=%llu best_target_waited_body_us=%llu"
			" best_target_waited_membership_calls=%llu best_target_waited_membership_us=%llu"
			" best_target_waited_projectionsetup_calls=%llu best_target_waited_projectionsetup_us=%llu"
			" best_target_waited_targetprobes_calls=%llu best_target_waited_targetprobes_us=%llu"
			" best_target_waited_target_probe_preparation_calls=%llu best_target_waited_target_probe_preparation_us=%llu"
			" best_target_waited_target_probe_main_damage_calls=%llu best_target_waited_target_probe_main_damage_us=%llu"
			" best_target_waited_target_probe_dps_score_calls=%llu best_target_waited_target_probe_dps_score_us=%llu"
			" best_target_waited_appliedattacks_calls=%llu best_target_waited_appliedattacks_us=%llu"
			" best_target_waited_activation_start_calls=%llu best_target_waited_activation_start_us=%llu"
			" best_target_waited_activation_end_calls=%llu best_target_waited_activation_end_us=%llu"
			" best_target_waited_round_advance_calls=%llu best_target_waited_round_advance_us=%llu"
			" best_target_waited_remainder_us=%llu"
			" best_target_normal_exchange_calls=%llu best_target_normal_exchange_us=%llu"
			" best_target_normal_body_calls=%llu best_target_normal_body_us=%llu"
			" best_target_normal_membership_calls=%llu best_target_normal_membership_us=%llu"
			" best_target_normal_projectionsetup_calls=%llu best_target_normal_projectionsetup_us=%llu"
			" best_target_normal_targetprobes_calls=%llu best_target_normal_targetprobes_us=%llu"
			" best_target_normal_target_probe_preparation_calls=%llu best_target_normal_target_probe_preparation_us=%llu"
			" best_target_normal_target_probe_main_damage_calls=%llu best_target_normal_target_probe_main_damage_us=%llu"
			" best_target_normal_target_probe_dps_score_calls=%llu best_target_normal_target_probe_dps_score_us=%llu"
			" best_target_normal_appliedattacks_calls=%llu best_target_normal_appliedattacks_us=%llu"
			" best_target_normal_activation_start_calls=%llu best_target_normal_activation_start_us=%llu"
			" best_target_normal_activation_end_calls=%llu best_target_normal_activation_end_us=%llu"
			" best_target_normal_round_advance_calls=%llu best_target_normal_round_advance_us=%llu"
			" best_target_normal_remainder_us=%llu"
			" best_target_remainder_us=%llu"
			" select_siege_defense_calls=%llu select_siege_defense_us=%llu"
			" select_defensive_stance_calls=%llu select_defensive_stance_us=%llu"
			" hero_action_including_submit_calls=%llu hero_action_including_submit_us=%llu"
			" direct_unit_submit_calls=%llu direct_unit_submit_us=%llu unclassified_us=%llu",
			battleID.getNum(), player.toString(),
			static_cast<unsigned long long>(result->battleWallMicroseconds),
			static_cast<unsigned long long>(result->activeStackCalls),
			static_cast<unsigned long long>(result->activeStackTotalMicroseconds),
			static_cast<unsigned long long>(result->activeStackMaxMicroseconds), result->roundsObserved,
			static_cast<unsigned long long>(result->evaluatorConstruction.calls),
			static_cast<unsigned long long>(result->evaluatorConstruction.totalMicroseconds),
			static_cast<unsigned long long>(result->stackActionSelection.calls),
			static_cast<unsigned long long>(result->stackActionSelection.totalMicroseconds),
			static_cast<unsigned long long>(result->selectionCreatureSpell.calls),
			static_cast<unsigned long long>(result->selectionCreatureSpell.totalMicroseconds),
			static_cast<unsigned long long>(result->selectionMovementSearch.calls),
			static_cast<unsigned long long>(result->selectionMovementSearch.totalMicroseconds),
			static_cast<unsigned long long>(result->movementFindMove.calls),
			static_cast<unsigned long long>(result->movementFindMove.totalMicroseconds),
			static_cast<unsigned long long>(result->movementGoTowards.calls),
			static_cast<unsigned long long>(result->movementGoTowards.totalMicroseconds),
			static_cast<unsigned long long>(result->movementRemainderMicroseconds),
			static_cast<unsigned long long>(result->movementFindMoveExchangeSubset.calls),
			static_cast<unsigned long long>(result->movementFindMoveExchangeSubset.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeBody.calls),
			static_cast<unsigned long long>(result->movementExchangeBody.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeMembership.calls),
			static_cast<unsigned long long>(result->movementExchangeMembership.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeProjectionSetup.calls),
			static_cast<unsigned long long>(result->movementExchangeProjectionSetup.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeTargetProbes.calls),
			static_cast<unsigned long long>(result->movementExchangeTargetProbes.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeTargetProbePreparation.calls),
			static_cast<unsigned long long>(result->movementExchangeTargetProbePreparation.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeTargetProbeMainDamage.calls),
			static_cast<unsigned long long>(result->movementExchangeTargetProbeMainDamage.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeTargetProbeDpsScore.calls),
			static_cast<unsigned long long>(result->movementExchangeTargetProbeDpsScore.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeAppliedAttacks.calls),
			static_cast<unsigned long long>(result->movementExchangeAppliedAttacks.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeActivationStart.calls),
			static_cast<unsigned long long>(result->movementExchangeActivationStart.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeActivationEnd.calls),
			static_cast<unsigned long long>(result->movementExchangeActivationEnd.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeRoundAdvance.calls),
			static_cast<unsigned long long>(result->movementExchangeRoundAdvance.totalMicroseconds),
			static_cast<unsigned long long>(result->movementExchangeRemainderMicroseconds),
			static_cast<unsigned long long>(result->movementGoBlockingSubset.calls),
			static_cast<unsigned long long>(result->movementGoBlockingSubset.totalMicroseconds),
			static_cast<unsigned long long>(result->selectionTargetExchange.calls),
			static_cast<unsigned long long>(result->selectionTargetExchange.totalMicroseconds),
			static_cast<unsigned long long>(result->selectionRemainderMicroseconds),
			static_cast<unsigned long long>(result->selectionBestTarget.calls),
			static_cast<unsigned long long>(result->selectionBestTarget.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedExchange.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedExchange.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedBody.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedBody.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedMembership.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedMembership.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedProjectionSetup.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedProjectionSetup.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbes.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbes.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbePreparation.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbePreparation.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbeMainDamage.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbeMainDamage.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbeDpsScore.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedTargetProbeDpsScore.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedAppliedAttacks.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedAppliedAttacks.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedActivationStart.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedActivationStart.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedActivationEnd.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedActivationEnd.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedRoundAdvance.calls),
			static_cast<unsigned long long>(result->bestTargetWaitedRoundAdvance.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetWaitedRemainderMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalExchange.calls),
			static_cast<unsigned long long>(result->bestTargetNormalExchange.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalBody.calls),
			static_cast<unsigned long long>(result->bestTargetNormalBody.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalMembership.calls),
			static_cast<unsigned long long>(result->bestTargetNormalMembership.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalProjectionSetup.calls),
			static_cast<unsigned long long>(result->bestTargetNormalProjectionSetup.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbes.calls),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbes.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbePreparation.calls),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbePreparation.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbeMainDamage.calls),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbeMainDamage.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbeDpsScore.calls),
			static_cast<unsigned long long>(result->bestTargetNormalTargetProbeDpsScore.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalAppliedAttacks.calls),
			static_cast<unsigned long long>(result->bestTargetNormalAppliedAttacks.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalActivationStart.calls),
			static_cast<unsigned long long>(result->bestTargetNormalActivationStart.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalActivationEnd.calls),
			static_cast<unsigned long long>(result->bestTargetNormalActivationEnd.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalRoundAdvance.calls),
			static_cast<unsigned long long>(result->bestTargetNormalRoundAdvance.totalMicroseconds),
			static_cast<unsigned long long>(result->bestTargetNormalRemainderMicroseconds),
			static_cast<unsigned long long>(result->bestTargetRemainderMicroseconds),
			static_cast<unsigned long long>(result->selectionSiegeDefense.calls),
			static_cast<unsigned long long>(result->selectionSiegeDefense.totalMicroseconds),
			static_cast<unsigned long long>(result->selectionDefensiveStance.calls),
			static_cast<unsigned long long>(result->selectionDefensiveStance.totalMicroseconds),
			static_cast<unsigned long long>(result->heroAction.calls),
			static_cast<unsigned long long>(result->heroAction.totalMicroseconds),
			static_cast<unsigned long long>(result->directUnitSubmission.calls),
			static_cast<unsigned long long>(result->directUnitSubmission.totalMicroseconds),
			static_cast<unsigned long long>(result->unclassifiedMicroseconds));
		const auto logProbeCache = [this](const char * context, const DamageCacheProbeTiming & cache)
		{
			if(cache.body.calls == 0)
				return;
			logAi->info("BattleAI nested probe cache timing battle=%d player=%s context=%s"
				" body_calls=%llu body_ns=%llu"
				" preflight_calls=%llu preflight_ns=%llu dynamic_calls=%llu dynamic_ns=%llu remainder_ns=%llu"
				" current_calls=%llu original_calls=%llu root_hits=%llu child_hits=%llu parent_original_hits=%llu"
				" root_original_fallbacks=%llu parent_original_fallbacks=%llu dynamic_bypasses=%llu"
				" fills=%llu dead_returns=%llu failed_operations=%llu suppressed_reentries=%llu",
				battleID.getNum(), player.toString(), context, static_cast<unsigned long long>(cache.body.calls),
				static_cast<unsigned long long>(cache.body.totalNanoseconds),
				static_cast<unsigned long long>(cache.preflight.calls), static_cast<unsigned long long>(cache.preflight.totalNanoseconds),
				static_cast<unsigned long long>(cache.dynamicDamage.calls), static_cast<unsigned long long>(cache.dynamicDamage.totalNanoseconds),
				static_cast<unsigned long long>(cache.remainderNanoseconds),
				static_cast<unsigned long long>(cache.getDamageCalls), static_cast<unsigned long long>(cache.getOriginalDamageCalls),
				static_cast<unsigned long long>(cache.rootCacheHits), static_cast<unsigned long long>(cache.localChildCacheHits),
				static_cast<unsigned long long>(cache.parentOriginalHits), static_cast<unsigned long long>(cache.rootOriginalFallbacks),
				static_cast<unsigned long long>(cache.parentOriginalFallbacks), static_cast<unsigned long long>(cache.dynamicBypasses),
				static_cast<unsigned long long>(cache.cacheFillCalls), static_cast<unsigned long long>(cache.deadReturns),
				static_cast<unsigned long long>(cache.failedOperations), static_cast<unsigned long long>(cache.suppressedReentrantCalls));
		};
		logProbeCache("movement", result->movementExchangeProbeDamageCache);
		logProbeCache("best_target_waited", result->bestTargetWaitedProbeDamageCache);
		logProbeCache("best_target_normal", result->bestTargetNormalProbeDamageCache);
		const auto logSubsets = [](const char * context, const StageTiming & expiry,
			const StageTiming & gambler, const StageTiming & roundTimed)
		{
			logAi->info("BattleAI nested lifecycle timing context=%s activation_expiry_removal_calls=%llu"
				" activation_expiry_removal_us=%llu activation_gambler_removal_calls=%llu"
				" activation_gambler_removal_us=%llu round_timed_updates_calls=%llu round_timed_updates_us=%llu",
				context, static_cast<unsigned long long>(expiry.calls), static_cast<unsigned long long>(expiry.totalMicroseconds),
				static_cast<unsigned long long>(gambler.calls), static_cast<unsigned long long>(gambler.totalMicroseconds),
				static_cast<unsigned long long>(roundTimed.calls), static_cast<unsigned long long>(roundTimed.totalMicroseconds));
		};
		logSubsets("movement", result->movementExchangeActivationExpiryRemoval,
			result->movementExchangeActivationGamblerRemoval, result->movementExchangeRoundTimedUpdates);
		logSubsets("best_target_waited", result->bestTargetWaitedActivationExpiryRemoval,
			result->bestTargetWaitedActivationGamblerRemoval, result->bestTargetWaitedRoundTimedUpdates);
		logSubsets("best_target_normal", result->bestTargetNormalActivationExpiryRemoval,
			result->bestTargetNormalActivationGamblerRemoval, result->bestTargetNormalRoundTimedUpdates);
		const auto logEffectWork = [](const char * context, const char * lifecycle,
			const HypotheticLifecycleTiming::EffectWorkTiming & work)
		{
			for(const auto & [name, member] : {
				std::pair{"stacked_query", &HypotheticLifecycleTiming::EffectWorkTiming::stackedQuery},
				std::pair{"raw_query", &HypotheticLifecycleTiming::EffectWorkTiming::rawQuery},
				std::pair{"snapshot_materialization", &HypotheticLifecycleTiming::EffectWorkTiming::snapshotMaterialization},
				std::pair{"selective_removal_query", &HypotheticLifecycleTiming::EffectWorkTiming::selectiveRemovalQuery},
				std::pair{"removal_mutation", &HypotheticLifecycleTiming::EffectWorkTiming::removalMutation},
				std::pair{"round_aging", &HypotheticLifecycleTiming::EffectWorkTiming::roundAging},
			})
			{
				const auto & stage = work.*member;
				logAi->info("BattleAI nested effect work context=%s lifecycle=%s stage=%s calls=%llu us=%llu ns=%llu",
					context, lifecycle, name, static_cast<unsigned long long>(stage.calls),
					static_cast<unsigned long long>(stage.totalMicroseconds),
					static_cast<unsigned long long>(stage.totalNanoseconds));
			}
		};
		logEffectWork("movement", "activation_expiry", result->movementExchangeActivationExpiryWork);
		logEffectWork("movement", "activation_gambler", result->movementExchangeActivationGamblerWork);
		logEffectWork("movement", "round_timed", result->movementExchangeRoundTimedWork);
		logEffectWork("best_target_waited", "activation_expiry", result->bestTargetWaitedActivationExpiryWork);
		logEffectWork("best_target_waited", "activation_gambler", result->bestTargetWaitedActivationGamblerWork);
		logEffectWork("best_target_waited", "round_timed", result->bestTargetWaitedRoundTimedWork);
		logEffectWork("best_target_normal", "activation_expiry", result->bestTargetNormalActivationExpiryWork);
		logEffectWork("best_target_normal", "activation_gambler", result->bestTargetNormalActivationGamblerWork);
		logEffectWork("best_target_normal", "round_timed", result->bestTargetNormalRoundTimedWork);
	}
};

BattleEvaluator::SelectionTiming * CBattleAI::TimingScope::selectionSink()
{
	selectionTotals.movementDetails.findMoveExchangeDetails.collectProbeDamageCache = traceRequested;
	selectionTotals.bestTargetDetails.waitedDetails.collectProbeDamageCache = traceRequested;
	selectionTotals.bestTargetDetails.normalDetails.collectProbeDamageCache = traceRequested;
	return state ? &selectionTotals : nullptr;
}

void CBattleAI::TimingScope::submitUnitAction(std::shared_ptr<CBattleCallback> callback,
	const BattleID & battleID, const BattleAction & action)
{
	// Only a selected direct WAIT is certified here. Hero-action returns have
	// no invented submission timestamps; the owner may die during submission.
	if(traceRequested && action.actionType == EActionType::WAIT)
	{
		uint64_t attempt;
		{
			std::lock_guard lock(state->mutex);
			attempt = state->waitTraceAttempts++;
		}
		const auto limit = waitTimingLimit();
		if(attempt == limit)
			logAi->debug("NH_WAIT_CALLBACK_TIMING cap=%u battle=%d reached; subsequent WAIT records omitted", limit, state->battleID.getNum());
		traceWait = attempt < limit;
		if(traceWait)
			decisionReady = TimingState::Clock::now();
	}
	measure(&TimingSummary::directUnitSubmission, [&]
	{
		if(traceWait)
			submitBegin = TimingState::Clock::now();
		try
		{
			callback->battleMakeUnitAction(battleID, action);
			if(traceWait)
			{
				submitEnd = TimingState::Clock::now();
				submitReturned = true;
			}
		}
		catch(...)
		{
			if(traceWait)
			{
				submitEnd = TimingState::Clock::now();
				submitThrew = true;
			}
			throw;
		}
	});
}

CBattleAI::TimingScope::TimingScope(std::shared_ptr<TimingState> current, const BattleID & battleID, const CStack * stack) : state(std::move(current))
{
	if(!state || state->battleID != battleID)
	{
		state.reset();
		return;
	}
	bool accepted = false;
	{
		std::lock_guard lock(state->mutex);
		if(!state->summary.finished)
		{
			++state->summary.activeStackCalls;
			callbackOrdinal = state->summary.activeStackCalls;
			++state->summary.activeStackInFlight;
			accepted = true;
		}
	}
	if(!accepted)
		state.reset();
	traceRequested = state && waitTimingEnabled();
	if(traceRequested)
	{
		capturedStack = stack->unitId();
		capturedSide = stack->unitSide();
		capturedRound = stack->getBattle() ? stack->getBattle()->getRound() : -1;
		uncaughtAtEntry = std::uncaught_exceptions();
	}
}

CBattleAI::TimingScope::~TimingScope()
{
	if(!state)
		return;
	const auto exited = TimingState::Clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
		exited - started).count();
	std::optional<TimingSummary> report;
	{
		std::lock_guard lock(state->mutex);
		uint64_t classifiedMicroseconds = 0;
		for(auto stage : {&TimingSummary::evaluatorConstruction, &TimingSummary::stackActionSelection,
			&TimingSummary::heroAction, &TimingSummary::directUnitSubmission})
		{
			const auto & local = stageTotals.*stage;
			auto & aggregate = state->summary.*stage;
			aggregate.calls += local.calls;
			aggregate.totalMicroseconds += local.totalMicroseconds;
			classifiedMicroseconds += local.totalMicroseconds;
		}
		const auto accumulateSelection = [](StageTiming & aggregate,
			const auto & local)
		{
			aggregate.calls += local.calls;
			aggregate.totalMicroseconds += local.totalMicroseconds;
		};
		accumulateSelection(state->summary.selectionCreatureSpell, selectionTotals.creatureSpell);
		const auto accumulateEffectWork = [](HypotheticLifecycleTiming::EffectWorkTiming & aggregate,
			const HypotheticLifecycleTiming::EffectWorkTiming & sample)
		{
			for(auto member : {&HypotheticLifecycleTiming::EffectWorkTiming::stackedQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::rawQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::snapshotMaterialization,
				&HypotheticLifecycleTiming::EffectWorkTiming::selectiveRemovalQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::removalMutation,
				&HypotheticLifecycleTiming::EffectWorkTiming::roundAging})
			{
				(aggregate.*member).accumulate(sample.*member);
			}
		};
		accumulateSelection(state->summary.selectionMovementSearch, selectionTotals.movementSearch);
		const auto & movement = selectionTotals.movementDetails;
		accumulateSelection(state->summary.movementFindMove, movement.findMove);
		accumulateSelection(state->summary.movementGoTowards, movement.goTowards);
		accumulateSelection(state->summary.movementFindMoveExchangeSubset, movement.findMoveExchange);
		const auto & exchange = movement.findMoveExchangeDetails;
		accumulateSelection(state->summary.movementExchangeBody, exchange.body);
		accumulateSelection(state->summary.movementExchangeMembership, exchange.membership);
		accumulateSelection(state->summary.movementExchangeProjectionSetup, exchange.projectionSetup);
		accumulateSelection(state->summary.movementExchangeTargetProbes, exchange.targetProbes);
		accumulateSelection(state->summary.movementExchangeTargetProbePreparation, exchange.targetProbePreparation);
		accumulateSelection(state->summary.movementExchangeTargetProbeMainDamage, exchange.targetProbeMainDamage);
		accumulateSelection(state->summary.movementExchangeTargetProbeDpsScore, exchange.targetProbeDpsScore);
		if(traceRequested)
			state->summary.movementExchangeProbeDamageCache.accumulate(exchange.targetProbeDamageCache);
		accumulateSelection(state->summary.movementExchangeAppliedAttacks, exchange.appliedAttacks);
		accumulateSelection(state->summary.movementExchangeActivationStart, exchange.activationStart);
		accumulateSelection(state->summary.movementExchangeActivationEnd, exchange.activationEnd);
		accumulateSelection(state->summary.movementExchangeRoundAdvance, exchange.roundAdvance);
		accumulateSelection(state->summary.movementExchangeActivationExpiryRemoval, exchange.lifecycleSubsets.activationExpiryRemoval);
		accumulateSelection(state->summary.movementExchangeActivationGamblerRemoval, exchange.lifecycleSubsets.activationGamblerRemoval);
		accumulateSelection(state->summary.movementExchangeRoundTimedUpdates, exchange.lifecycleSubsets.roundTimedUpdates);
		accumulateEffectWork(state->summary.movementExchangeActivationExpiryWork, exchange.lifecycleSubsets.activationExpiryWork);
		accumulateEffectWork(state->summary.movementExchangeActivationGamblerWork, exchange.lifecycleSubsets.activationGamblerWork);
		accumulateEffectWork(state->summary.movementExchangeRoundTimedWork, exchange.lifecycleSubsets.roundTimedWork);
		state->summary.movementExchangeRemainderMicroseconds += exchange.remainderMicroseconds;
		accumulateSelection(state->summary.movementGoBlockingSubset, movement.goTowardsBlocking);
		state->summary.movementRemainderMicroseconds += selectionTotals.movementSearch.totalMicroseconds
			- movement.findMove.totalMicroseconds - movement.goTowards.totalMicroseconds;
		accumulateSelection(state->summary.selectionTargetExchange, selectionTotals.targetExchange);
		accumulateSelection(state->summary.selectionBestTarget, selectionTotals.bestTarget);
		const auto & bestTarget = selectionTotals.bestTargetDetails;
		accumulateSelection(state->summary.bestTargetWaitedExchange, bestTarget.waitedExchange);
		accumulateSelection(state->summary.bestTargetWaitedBody, bestTarget.waitedDetails.body);
		accumulateSelection(state->summary.bestTargetWaitedMembership, bestTarget.waitedDetails.membership);
		accumulateSelection(state->summary.bestTargetWaitedProjectionSetup, bestTarget.waitedDetails.projectionSetup);
		accumulateSelection(state->summary.bestTargetWaitedTargetProbes, bestTarget.waitedDetails.targetProbes);
		accumulateSelection(state->summary.bestTargetWaitedTargetProbePreparation, bestTarget.waitedDetails.targetProbePreparation);
		accumulateSelection(state->summary.bestTargetWaitedTargetProbeMainDamage, bestTarget.waitedDetails.targetProbeMainDamage);
		accumulateSelection(state->summary.bestTargetWaitedTargetProbeDpsScore, bestTarget.waitedDetails.targetProbeDpsScore);
		if(traceRequested)
			state->summary.bestTargetWaitedProbeDamageCache.accumulate(bestTarget.waitedDetails.targetProbeDamageCache);
		accumulateSelection(state->summary.bestTargetWaitedAppliedAttacks, bestTarget.waitedDetails.appliedAttacks);
		accumulateSelection(state->summary.bestTargetWaitedActivationStart, bestTarget.waitedDetails.activationStart);
		accumulateSelection(state->summary.bestTargetWaitedActivationEnd, bestTarget.waitedDetails.activationEnd);
		accumulateSelection(state->summary.bestTargetWaitedRoundAdvance, bestTarget.waitedDetails.roundAdvance);
		accumulateSelection(state->summary.bestTargetWaitedActivationExpiryRemoval, bestTarget.waitedDetails.lifecycleSubsets.activationExpiryRemoval);
		accumulateSelection(state->summary.bestTargetWaitedActivationGamblerRemoval, bestTarget.waitedDetails.lifecycleSubsets.activationGamblerRemoval);
		accumulateSelection(state->summary.bestTargetWaitedRoundTimedUpdates, bestTarget.waitedDetails.lifecycleSubsets.roundTimedUpdates);
		accumulateEffectWork(state->summary.bestTargetWaitedActivationExpiryWork, bestTarget.waitedDetails.lifecycleSubsets.activationExpiryWork);
		accumulateEffectWork(state->summary.bestTargetWaitedActivationGamblerWork, bestTarget.waitedDetails.lifecycleSubsets.activationGamblerWork);
		accumulateEffectWork(state->summary.bestTargetWaitedRoundTimedWork, bestTarget.waitedDetails.lifecycleSubsets.roundTimedWork);
		state->summary.bestTargetWaitedRemainderMicroseconds += bestTarget.waitedDetails.remainderMicroseconds;
		accumulateSelection(state->summary.bestTargetNormalExchange, bestTarget.normalExchange);
		accumulateSelection(state->summary.bestTargetNormalBody, bestTarget.normalDetails.body);
		accumulateSelection(state->summary.bestTargetNormalMembership, bestTarget.normalDetails.membership);
		accumulateSelection(state->summary.bestTargetNormalProjectionSetup, bestTarget.normalDetails.projectionSetup);
		accumulateSelection(state->summary.bestTargetNormalTargetProbes, bestTarget.normalDetails.targetProbes);
		accumulateSelection(state->summary.bestTargetNormalTargetProbePreparation, bestTarget.normalDetails.targetProbePreparation);
		accumulateSelection(state->summary.bestTargetNormalTargetProbeMainDamage, bestTarget.normalDetails.targetProbeMainDamage);
		accumulateSelection(state->summary.bestTargetNormalTargetProbeDpsScore, bestTarget.normalDetails.targetProbeDpsScore);
		if(traceRequested)
			state->summary.bestTargetNormalProbeDamageCache.accumulate(bestTarget.normalDetails.targetProbeDamageCache);
		accumulateSelection(state->summary.bestTargetNormalAppliedAttacks, bestTarget.normalDetails.appliedAttacks);
		accumulateSelection(state->summary.bestTargetNormalActivationStart, bestTarget.normalDetails.activationStart);
		accumulateSelection(state->summary.bestTargetNormalActivationEnd, bestTarget.normalDetails.activationEnd);
		accumulateSelection(state->summary.bestTargetNormalRoundAdvance, bestTarget.normalDetails.roundAdvance);
		accumulateSelection(state->summary.bestTargetNormalActivationExpiryRemoval, bestTarget.normalDetails.lifecycleSubsets.activationExpiryRemoval);
		accumulateSelection(state->summary.bestTargetNormalActivationGamblerRemoval, bestTarget.normalDetails.lifecycleSubsets.activationGamblerRemoval);
		accumulateSelection(state->summary.bestTargetNormalRoundTimedUpdates, bestTarget.normalDetails.lifecycleSubsets.roundTimedUpdates);
		accumulateEffectWork(state->summary.bestTargetNormalActivationExpiryWork, bestTarget.normalDetails.lifecycleSubsets.activationExpiryWork);
		accumulateEffectWork(state->summary.bestTargetNormalActivationGamblerWork, bestTarget.normalDetails.lifecycleSubsets.activationGamblerWork);
		accumulateEffectWork(state->summary.bestTargetNormalRoundTimedWork, bestTarget.normalDetails.lifecycleSubsets.roundTimedWork);
		state->summary.bestTargetNormalRemainderMicroseconds += bestTarget.normalDetails.remainderMicroseconds;
		state->summary.bestTargetRemainderMicroseconds += selectionTotals.bestTarget.totalMicroseconds
			- bestTarget.waitedExchange.totalMicroseconds - bestTarget.normalExchange.totalMicroseconds;

		accumulateSelection(state->summary.selectionSiegeDefense, selectionTotals.siegeDefense);
		accumulateSelection(state->summary.selectionDefensiveStance, selectionTotals.defensiveStance);
		const uint64_t selectionClassifiedMicroseconds = selectionTotals.creatureSpell.totalMicroseconds
			+ selectionTotals.movementSearch.totalMicroseconds + selectionTotals.targetExchange.totalMicroseconds;
		state->summary.selectionRemainderMicroseconds += stageTotals.stackActionSelection.totalMicroseconds
			- selectionClassifiedMicroseconds;
		state->summary.unclassifiedMicroseconds += static_cast<uint64_t>(elapsed) - classifiedMicroseconds;
		state->summary.activeStackTotalMicroseconds += elapsed;
		state->summary.activeStackMaxMicroseconds = std::max(
			state->summary.activeStackMaxMicroseconds, static_cast<uint64_t>(elapsed));
		--state->summary.activeStackInFlight;
		report = state->reportLocked();
	}
	// No CBattleAI pointer: battleEnd may have destroyed the AI during submission.
	state->log(report);
	if(traceWait)
	{
		const auto absolute = [](TimingState::Clock::time_point point)
		{
			return static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(point.time_since_epoch()).count());
		};
		// Epilogue sample before this record, not CPU time or physical presentation.
		// Epochs correlate only within this process, never across native/Wine runs.
		const auto callbackExit = TimingState::Clock::now();
		logAi->debug("NH_WAIT_CALLBACK_TIMING battle=%d player=%d side=%d stack=%u round=%d callback=%llu entry_steady_us=%lld decision_ready_steady_us=%lld submit_begin_steady_us=%lld submit_end_steady_us=%lld exit_steady_us=%lld submit_returned=%d submit_threw=%d callback_unwinding=%d round_valid=%d",
			state->battleID.getNum(), state->player.getNum(), static_cast<int>(capturedSide), capturedStack,
			capturedRound, static_cast<unsigned long long>(callbackOrdinal), absolute(started), absolute(decisionReady),
			absolute(submitBegin), absolute(submitEnd), absolute(callbackExit), submitReturned, submitThrew,
			std::uncaught_exceptions() > uncaughtAtEntry, capturedRound >= 0);
	}
}

std::optional<CBattleAI::TimingSummary> CBattleAI::getTimingSummary() const
{
	const auto current = timingState.load();
	if(!current)
		return std::nullopt;
	std::lock_guard lock(current->mutex);
	return current->snapshotLocked();
}

std::shared_ptr<CBattleAI::TimingState> CBattleAI::makeTimingState(BattleID battleID, PlayerColor player)
{
	return std::make_shared<TimingState>(battleID, player);
}

void CBattleAI::battleEnd(const BattleID & battleID, const BattleResult * result, QueryID queryID)
{
	const auto current = timingState.load();
	if(!current || current->battleID != battleID)
		return;
	std::optional<TimingSummary> report;
	{
		std::lock_guard lock(current->mutex);
		if(current->summary.finished)
			return;
		current->ended = TimingState::Clock::now();
		current->summary.finished = true;
		report = current->reportLocked();
	}
	current->log(report);
}

void CBattleAI::battleNewRound(const BattleID & battleID)
{
	const auto current = timingState.load();
	const auto battle = cb->getBattle(battleID);
	if(!current || current->battleID != battleID || !battle)
		return;
	const auto round = battle->battleGetRound();
	std::lock_guard lock(current->mutex);
	if(!current->summary.finished && round >= 0 && round != current->lastObservedRound)
	{
		current->lastObservedRound = round;
		++current->summary.roundsObserved;
	}
}
