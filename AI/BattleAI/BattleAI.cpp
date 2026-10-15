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
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "tbb/parallel_for.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CStack.h" // TODO: remove
                              // Eventually only IBattleInfoCallback and battle::Unit should be used,
                              // CUnitState should be private and CStack should be removed completely
#include "../../lib/logging/VisualLogger.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string_view>
#include <utility>

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

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

class CBattleAI::TimingScope
{
	std::shared_ptr<TimingState> state;
	TimingState::Clock::time_point started = TimingState::Clock::now();
	TimingSummary stageTotals;
	BattleEvaluator::SelectionTiming selectionTotals;
	bool traceRequested = false;
	bool traceWait = false;
	bool submitReturned = false;
	bool submitThrew = false;
	uint64_t callbackOrdinal = 0;
	uint32_t capturedStack = 0;
	int32_t capturedRound = -1;
	BattleSide capturedSide = BattleSide::NONE;
	int uncaughtAtEntry = 0;
	TimingState::Clock::time_point decisionReady;
	TimingState::Clock::time_point submitBegin;
	TimingState::Clock::time_point submitEnd;

	class StageScope
	{
		StageTiming & timing;
		TimingState::Clock::time_point started = TimingState::Clock::now();
	public:
		explicit StageScope(StageTiming & timing) : timing(timing) {}
		~StageScope()
		{
			++timing.calls;
			timing.totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				TimingState::Clock::now() - started).count();
		}
	};
public:
	BattleEvaluator::SelectionTiming * selectionSink()
	{
		selectionTotals.movementDetails.findMoveExchangeDetails.collectProbeDamageCache = traceRequested;
		selectionTotals.bestTargetDetails.waitedDetails.collectProbeDamageCache = traceRequested;
		selectionTotals.bestTargetDetails.normalDetails.collectProbeDamageCache = traceRequested;
		return state ? &selectionTotals : nullptr;
	}

	/// The measured regions are non-nested within this callback. Stage data is
	/// local until callback exit, including when submission destroys the AI owner.
	template<typename Callable>
	decltype(auto) measure(StageTiming TimingSummary::* stage, Callable && action)
	{
		StageScope measurement(stageTotals.*stage);
		return std::forward<Callable>(action)();
	}

	void submitUnitAction(std::shared_ptr<CBattleCallback> callback,
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

	TimingScope(std::shared_ptr<TimingState> current, const BattleID & battleID, const CStack * stack) : state(std::move(current))
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

	~TimingScope()
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
};

std::optional<CBattleAI::TimingSummary> CBattleAI::getTimingSummary() const
{
	const auto current = timingState.load();
	if(!current)
		return std::nullopt;
	std::lock_guard lock(current->mutex);
	return current->snapshotLocked();
}

CBattleAI::CBattleAI()
	: side(BattleSide::NONE),
	wasWaitingForRealize(false)
{
}

CBattleAI::~CBattleAI()
{
	if(cb)
	{
		//Restore previous state of CB - it may be shared with the main AI (like VCAI)
		cb->waitTillRealize = wasWaitingForRealize;
	}
}

void logHexNumbers()
{
#if BATTLE_TRACE_LEVEL >= 1
	logVisual->updateWithLock("hexes", [](IVisualLogBuilder & b)
		{
			for(BattleHex hex = BattleHex(0); hex < GameConstants::BFIELD_SIZE; ++hex)
				b.addText(hex, std::to_string(hex.toInt()));
		});
#endif
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	playerID = *CB->getPlayerID();
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false;
	movesSkippedByDefense = 0;

	logHexNumbers();
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences)
{
	initBattleInterface(ENV, CB);
	autobattlePreferences = autocombatPreferences;
}

BattleAction CBattleAI::useHealingTent(const BattleID & battleID, const CStack *stack)
{
	const auto battle = cb->getBattle(battleID);
	const auto currentControllerSide = battle->playerToSide(battle->battleGetActionController(stack));
	const auto makeActionForUnitSide = [stack](BattleAction action)
	{
		action.side = stack->unitSide();
		return action;
	};
	const auto defend = [&]()
	{
		return makeActionForUnitSide(BattleAction::makeDefend(stack));
	};
	const auto controllerFriendlyStacks = [&]()
	{
		std::vector<const CStack *> result;
		for(const auto * target : battle->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
			if(battle->battleMatchActionController(stack, target, true))
				result.push_back(target);
		return result;
	};
	if(!stack->isFirstAidTent())
	{
		const auto healingTargets = controllerFriendlyStacks();
		std::map<int, const CStack *> woundHpToStack;
		for(const auto * target : healingTargets)
			if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				woundHpToStack[woundHp] = target;
		if(woundHpToStack.empty())
			return defend();
		return makeActionForUnitSide(BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second));
	}

	const auto * tentOwner = battle->battleGetOwnerHero(stack);
	const bool surgeon = tentOwner && tentOwner->hasActivePerk(
		"new-horizons:warMachines", "new-horizons:warMachines.surgeon");
	const bool canonicalSiegeRules = tentOwner
		&& tentOwner->getCapabilityRules()["rulesetVersion"].Integer() >= 3;
	if(!canonicalSiegeRules)
	{
		std::map<int, const CStack *> woundHpToStack;
		for(const auto * target : controllerFriendlyStacks())
			if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				woundHpToStack[woundHp] = target;

		if(surgeon)
		{
			std::map<int, const CStack *> controllerWoundHpToStack;
			std::map<int, const CStack *> afflictionWoundHpToStack;
			for(const auto * target : battle->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
			{
				if(!target->alive() || !target->canBeHealed()
					|| !battle->battleMatchActionController(stack, target, true))
					continue;

				if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				{
					controllerWoundHpToStack[woundHp] = target;
					if(physicalAfflictions::first(*target))
						afflictionWoundHpToStack[woundHp] = target;
				}
			}

			if(!afflictionWoundHpToStack.empty())
				return makeActionForUnitSide(
					BattleAction::makeHeal(stack, afflictionWoundHpToStack.rbegin()->second));
			if(!controllerWoundHpToStack.empty())
				return makeActionForUnitSide(
					BattleAction::makeHeal(stack, controllerWoundHpToStack.rbegin()->second));
			return defend();
		}

		if(woundHpToStack.empty())
			return defend();
		return makeActionForUnitSide(
			BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second));
	}

	const auto healingOutput = battle->battleGetFirstAidHealingOutput(stack);
	if(healingOutput <= 0
		|| (currentControllerSide != BattleSide::ATTACKER && currentControllerSide != BattleSide::DEFENDER))
		return defend();

	using HealingRank = std::pair<int64_t, int64_t>;
	std::optional<std::pair<HealingRank, const CStack *>> bestControllerTarget;
	std::optional<std::pair<HealingRank, const CStack *>> bestAfflictionTarget;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		const auto * target = dynamic_cast<const CStack *>(unit);
		if(!target || !battle->battleCanHealWithFirstAidTent(stack, target))
			continue;

		const auto preview = battle->battleGetFirstAidHealingPreview(stack, target);
		if(preview.totalHealedHP() <= 0)
			continue;

		const auto missingHealth = std::max<int64_t>(0,
			target->getMaxHealth() - target->getFirstHPleft());
		const HealingRank rank{preview.totalHealedHP(), missingHealth};
		const auto candidate = std::pair{rank, target};
		if(!bestControllerTarget || candidate.first > bestControllerTarget->first)
			bestControllerTarget = candidate;
		if(surgeon && !battle->battleCanRepairWarMachine(stack, target)
			&& preview.survivorHealedHP > 0 && physicalAfflictions::first(*target)
			&& (!bestAfflictionTarget || candidate.first > bestAfflictionTarget->first))
			bestAfflictionTarget = candidate;
	}

	const auto selected = bestAfflictionTarget ? bestAfflictionTarget : bestControllerTarget;
	FirstAidStructureRepairPreview structure;
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
	{
		const auto candidate = battle->battleGetFirstAidStructureRepairPreview(stack, static_cast<EWallPart>(i));
		if(candidate.repairedHP() > structure.repairedHP())
			structure = candidate;
	}
	if(structure.repairedHP() > 0 && (!selected || structure.repairedHP() > selected->first.first))
	{
		BattleAction repair;
		repair.actionType = EActionType::STACK_HEAL;
		repair.side = currentControllerSide;
		repair.stackNumber = stack->unitId();
		repair.aimToHex(battle->wallPartToBattleHex(structure.part));
		return repair;
	}
	if(!selected)
		return defend();
	return makeActionForUnitSide(BattleAction::makeHeal(stack, selected->second));
}

void CBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	tacticsHandler->onTacticsStarted();
}

void CBattleAI::actionFinished(const BattleID & battleID, const BattleAction & action)
{
	tacticsHandler->onActionFinished(action);
}

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

std::optional<BattleAction> chooseDemonicGate(const std::shared_ptr<CBattleInfoCallback> & battle,
	const CStack * source, BattleSide side)
{
	if(!battle || !source || source->unitSide() != side
		|| source->creatureId().toCreature()->getFactionID() != FactionID::INFERNO)
		return std::nullopt;
	const auto * hero = battle->battleGetFightingHero(side);
	if(!hero)
		return std::nullopt;
	const int rank = hero->getPerkSkillRank("new-horizons:demonicGating");
	if(rank <= 0)
		return std::nullopt;

	CreatureID chosen;
	uint64_t chosenValue = 0;
	for(const auto & [creature, count] : battle->getBattle()->getDemonicReserve(side))
	{
		const auto category = battle->battleGetCreatureCategory(creature);
		if(count <= 0 || !category || static_cast<int>(category->category) >= rank)
			continue;
		const uint64_t value = static_cast<uint64_t>(count) * creature.toCreature()->getAIValue();
		if(!chosen.hasValue() || value > chosenValue)
		{
			chosen = creature;
			chosenValue = value;
		}
	}
	if(!chosen.hasValue())
		return std::nullopt;

	const int placementRange = hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
	const bool mobileGate = hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate");
	const auto accessibility = battle->getAccessibility();
	std::vector<BattleHex> movementCandidates{source->getPosition()};
	if(mobileGate)
	{
		const auto movementAccessibility = battle->getAccessibility(source);
		const int movementLimit = static_cast<int>(source->getMovementRange(0) / 2);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			if(!candidate.isAvailable() || candidate == source->getPosition()
				|| !movementAccessibility.accessible(candidate, source))
				continue;
			const auto [path, distance] = battle->getPath(source->getPosition(), candidate, source);
			if(!path.empty() && distance >= 0 && distance <= movementLimit)
				movementCandidates.push_back(candidate);
		}
	}
	BattleHex best;
	BattleHex bestMovement = source->getPosition();
	int bestEnemyDistance = std::numeric_limits<int>::max();
	for(const auto movement : movementCandidates)
	{
		const BattleHex occupiedTail = source->doubleWide()
			? source->occupiedHex(movement) : BattleHex::INVALID;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			const BattleHex gatedTail = battle::Unit::occupiedHex(
				candidate, chosen.toCreature()->isDoubleWide(), side);
			if(!candidate.isAvailable() || candidate == movement || candidate == occupiedTail
				|| (gatedTail.isValid() && (gatedTail == movement || gatedTail == occupiedTail))
				|| BattleHex::getDistance(movement, candidate) > placementRange
				|| battle->battleGetUnitByPos(candidate, true)
				|| !battle->battleGetAllObstaclesOnPos(candidate, false).empty()
				|| !accessibility.accessible(candidate, chosen.toCreature()->isDoubleWide(), side))
				continue;
			int nearestEnemy = std::numeric_limits<int>::max();
			for(const auto * enemy : battle->battleAliveUnits(CBattleInfoEssentials::otherSide(side)))
				nearestEnemy = std::min(nearestEnemy, static_cast<int>(BattleHex::getDistance(candidate, enemy->getPosition())));
			if(nearestEnemy < bestEnemyDistance)
			{
				best = candidate;
				bestMovement = movement;
				bestEnemyDistance = nearestEnemy;
			}
		}
	}
	if(!best.isAvailable())
		return std::nullopt;

	BattleAction result;
	result.actionType = EActionType::DEMONIC_GATING;
	result.side = source->unitSide();
	result.stackNumber = source->unitId();
	result.gatingCreature = chosen;
	if(bestMovement != source->getPosition())
		result.aimToHex(bestMovement);
	result.aimToHex(best);
	return result;
}

BattleAction CBattleAI::choosePursuitMovement(const std::shared_ptr<CBattleInfoCallback> & battle,
	const CStack * source)
{
	const auto reachable = battle->battleGetAvailableHexes(source, false);
	const auto enemies = battle->battleAliveUnits(CBattleInfoEssentials::otherSide(source->unitSide()));
	BattleHex best = BattleHex::INVALID;
	int bestEnemyDistance = std::numeric_limits<int>::max();
	int bestTravelDistance = -1;
	const auto distances = battle->battleGetDistances(source, source->getPosition());
	for(const auto candidate : reachable)
	{
		if(candidate == source->getPosition())
			continue;
		int enemyDistance = std::numeric_limits<int>::max();
		for(const auto * enemy : enemies)
			for(const auto occupied : enemy->getHexes())
				enemyDistance = std::min(enemyDistance,
					static_cast<int>(BattleHex::getDistance(candidate, occupied)));
		const int travelDistance = distances[candidate.toInt()];
		if(enemyDistance < bestEnemyDistance
			|| (enemyDistance == bestEnemyDistance && travelDistance > bestTravelDistance))
		{
			best = candidate;
			bestEnemyDistance = enemyDistance;
			bestTravelDistance = travelDistance;
		}
	}
	return best.isAvailable() ? BattleAction::makeMove(source, best) : BattleAction::makeDefend(source);
}

void CBattleAI::activeStack(const BattleID & battleID, const CStack * stack )
{
	TimingScope timing(timingState.load(), battleID, stack);
	LOG_TRACE_PARAMS(logAi, "stack: %s", stack->nodeName());
	const auto battleCallback = cb->getBattle(battleID);
	const auto hasMandatoryOrder = [&]()
	{
		return battleCallback->getBattle()->getCrisisCommandState().choice()
			|| battleCallback->battleHasPendingDoubleCommand(side)
			|| battleCallback->battleHasPendingPreCombatOrder(side);
	};
	if(hasMandatoryOrder())
	{
		// Mandatory opening/continuation Orders precede every creature action,
		// including the special siege, gating, and healing-tent paths below. The
		// evaluator keeps legal choices even when their ordinary heuristic is nonpositive.
		auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
		{
			return BattleEvaluator(env, cb, stack, playerID, battleID, side,
				getStrengthRatio(battleCallback, side),
				getSimulationTurnsCount(env->game()->getStartInfo()));
		});
		if(timing.measure(&TimingSummary::heroAction, [&]
		{
			return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
		}))
			return;
		if(hasMandatoryOrder())
		{
			if(battleCallback->getBattle()->getCrisisCommandState().choice())
			{
				auto decline = BattleAction::makeNoAction(stack);
				decline.side = side;
				timing.submitUnitAction(cb, battleID, decline);
				return;
			}
			// Never substitute a creature action while the authority still has an
			// unresolved mandatory Order window.
			logAi->error("BattleAI could not resolve a pending mandatory Hero Order");
			return;
		}
	}

	auto timeElapsed = [](std::chrono::time_point<std::chrono::high_resolution_clock> start) -> uint64_t
	{
		auto end = std::chrono::high_resolution_clock::now();

		return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	};

	BattleAction result = BattleAction::makeDefend(stack);

	auto start = std::chrono::high_resolution_clock::now();

	// Time Stop deliberately keeps a physical stack in the queue while making
	// every creature action illegal.  An AI still receives the same
	// non-automatic activation packet as a human, so it must use that window for
	// a legal Hero Action when one is available, or submit the authoritative
	// no-op pass.  Falling through to the ordinary stack evaluator would submit
	// DEFEND/WAIT and leave the battle waiting forever on a stopped stack.
	if(stack->isTimeStopped())
	{
		const auto battleCallback = cb->getBattle(battleID);
		if(battleCallback->battleGetMyHero()
			&& (autobattlePreferences.enableSpellsUsage || battleCallback->battleUsesHeroCommands()))
		{
			auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
			{
				return BattleEvaluator(env, cb, stack, playerID, battleID, side,
					getStrengthRatio(battleCallback, side),
					getSimulationTurnsCount(env->game()->getStartInfo()));
			});
			if(evaluator.canCastSpell() && timing.measure(&TimingSummary::heroAction, [&]
			{
				return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
			}))
				return;
		}

		BattleAction pass = BattleAction::makeNoAction(stack);
		// The AI callback is dispatched for the action controller, while every
		// creature-action packet continues to identify the stack's physical side.
		pass.side = stack->unitSide();
		timing.submitUnitAction(cb, battleID, pass);
		return;
	}

	if(stack->pursuitMovementRemaining > 0)
	{
		const auto battleCallback = cb->getBattle(battleID);
		if(battleCallback->battleGetMyHero()
			&& (autobattlePreferences.enableSpellsUsage || battleCallback->battleUsesHeroCommands()))
		{
			auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
			{
				return BattleEvaluator(env, cb, stack, playerID, battleID, side,
					getStrengthRatio(battleCallback, side),
					getSimulationTurnsCount(env->game()->getStartInfo()));
			});
			if(evaluator.canCastSpell()
				&& timing.measure(&TimingSummary::heroAction, [&]
				{
					return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
				}))
				return;
		}
		timing.submitUnitAction(cb, battleID, choosePursuitMovement(battleCallback, stack));
		return;
	}

	if(stack->isCatapult())
	{
		timing.submitUnitAction(cb, battleID, useCatapult(battleID, stack));
		return;
	}
	if(auto gating = chooseDemonicGate(cb->getBattle(battleID), stack, side))
	{
		timing.submitUnitAction(cb, battleID, *gating);
		return;
	}
	if(stack->hasBonusOfType(BonusType::SIEGE_WEAPON) && stack->hasBonusOfType(BonusType::HEALER))
	{
		timing.submitUnitAction(cb, battleID, useHealingTent(battleID, stack));
		return;
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Build evaluator and targets");
#endif

	auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
	{
		return BattleEvaluator(env, cb, stack, playerID, battleID, side,
			getStrengthRatio(cb->getBattle(battleID), side),
			getSimulationTurnsCount(env->game()->getStartInfo()));
	});

	result = timing.measure(&TimingSummary::stackActionSelection, [&]
	{
		return evaluator.selectStackAction(stack, timing.selectionSink());
	});

	if((autobattlePreferences.enableSpellsUsage || cb->getBattle(battleID)->battleUsesHeroCommands()) && evaluator.canCastSpell())
	{
		auto spelCasted = timing.measure(&TimingSummary::heroAction, [&]
		{
			return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
		});

		if(spelCasted)
			return;
	}

	// Master Gunner's saved allowance is a same-activation continuation, not a
	// new creature turn. Hero Actions above may still be used first; after that,
	// the only creature action is a fresh, independently selected legal shot.
	// If the raw allowance is no longer usable (for example, every enemy became
	// untargetable after a Hero Action), pass it instead of falling through to a
	// move/wait/defend that the authority must reject.
	if(const auto unitState = stack->acquireState(); unitState && unitState->rangedFollowUpDamagePercent > 0)
	{
		const auto battleCallback = cb->getBattle(battleID);
		const battle::Unit * bestTarget = nullptr;
		int64_t bestExpectedDamage = std::numeric_limits<int64_t>::min();
		if(battleCallback->battleCanTakeRangedFollowUp(stack))
		{
			for(const auto * target : battleCallback->battleAliveUnits())
			{
				if(!target || !target->alive() || target->isGhost()
					|| battleCallback->battleMatchActionController(stack, target, true))
					continue;

				const bool hasLegalHex = std::ranges::any_of(target->getHexes(), [&](const BattleHex & hex)
				{
					return hex.isValid() && battleCallback->battleCanShootAction(stack, hex);
				});
				if(!hasLegalHex)
					continue;

				const auto expectedDamage = battleCallback->battleExpectedLuckDamage(
					BattleAttackInfo(stack, target, 0, true));
				if(!bestTarget || expectedDamage > bestExpectedDamage)
				{
					bestTarget = target;
					bestExpectedDamage = expectedDamage;
				}
			}
		}

		if(bestTarget)
		{
			BattleAction followUp = BattleAction::makeShotAttack(stack, bestTarget);
			timing.submitUnitAction(cb, battleID, followUp);
			return;
		}

		BattleAction pass = BattleAction::makeNoAction(stack);
		timing.submitUnitAction(cb, battleID, pass);
		return;
	}

	logAi->trace("Spellcast attempt completed in %lld", timeElapsed(start));

	if(auto action = considerFleeingOrSurrendering(battleID))
	{
		timing.submitUnitAction(cb, battleID, *action);
		return;
	}

	// Creature Catapult abilities are legal stack actions, distinct from the
	// Catapult war-machine branch above. Consider one only after ordinary Hero
	// Actions and typed continuations have had their opportunity to resolve.
	const auto currentBattle = cb->getBattle(battleID);
	if(currentBattle && shouldUseCreatureCatapult(*currentBattle, stack, result, playerID))
		result = useCatapult(battleID, stack);

	if(result.actionType == EActionType::DEFEND)
	{
		movesSkippedByDefense++;
	}
	else if(result.actionType != EActionType::WAIT)
	{
		movesSkippedByDefense = 0;
	}

	logAi->trace("BattleAI decision made in %lld", timeElapsed(start));

	timing.submitUnitAction(cb, battleID, result);
}

bool CBattleAI::shouldUseCreatureCatapult(CBattleInfoCallback & battle,
	const CStack * stack, const BattleAction & ordinaryAction, PlayerColor actionController)
{
	if(!stack || !stack->alive() || stack->isGhost() || stack->isTurret()
		|| stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| !stack->hasBonusOfType(BonusType::CATAPULT)
		|| battle.battleGetActionController(stack) != actionController)
		return false;
	if(ordinaryAction.actionType == EActionType::WAIT)
		return false;

	const auto * battleState = battle.getBattle();
	if(!battleState || battleState->getActiveStackID() < 0
		|| stack->unitId() != static_cast<uint32_t>(battleState->getActiveStackID()))
		return false;

	const BattleClientInterfaceData clientData{{}, 0};
	const auto legalActions = battle.getClientActionsForStack(stack, clientData);
	const PossiblePlayerBattleAction catapultAction{PossiblePlayerBattleAction::CATAPULT};
	if(std::ranges::find(legalActions, catapultAction) == legalActions.end())
		return false;

	const auto * defendedTown = battle.battleGetDefendedTown();
	if(!defendedTown || defendedTown->fortificationsLevel().wallsHealth <= 0)
		return false;

	const BattleSide actionSide = battle.playerToSide(actionController);
	const BattleSide townSide = battle.playerToSide(defendedTown->tempOwner);
	if((actionSide != BattleSide::ATTACKER && actionSide != BattleSide::DEFENDER)
		|| townSide != CBattleInfoEssentials::otherSide(actionSide))
		return false;

	if(battle.battleGetCatapultStructuralDamage(stack, 1) <= 0)
		return false;

	static constexpr std::array attackableWallParts{
		EWallPart::KEEP,
		EWallPart::BOTTOM_TOWER,
		EWallPart::UPPER_TOWER,
		EWallPart::BELOW_GATE,
		EWallPart::OVER_GATE,
		EWallPart::BOTTOM_WALL,
		EWallPart::UPPER_WALL,
		EWallPart::GATE
	};
	const bool hasUsefulStructure = std::ranges::any_of(attackableWallParts, [&](EWallPart part)
	{
		return battle.isWallPartAttackable(part) && battle.getWallStructuralHP(part) > 0;
	});
	if(!hasUsefulStructure)
		return false;

	const bool gateNeedsBreaching = battle.battleGetGateState() == EGateState::CLOSED
		&& battle.isWallPartAttackable(EWallPart::GATE)
		&& battle.getWallStructuralHP(EWallPart::GATE) > 0;
	if(gateNeedsBreaching)
	{
		bool alliedGroundForceOutside = false;
		bool hostileStackInside = false;
		for(const auto * candidate : battle.battleGetAllStacks())
		{
			if(!candidate || !candidate->alive() || candidate->isGhost() || candidate->isTurret()
				|| candidate->unitId() == stack->unitId())
				continue;

			const auto candidateOwnerSide = battle.playerToSide(battle.battleGetOwner(candidate));
			const auto candidateControllerSide = battle.playerToSide(battle.battleGetActionController(candidate));
			const bool hasOutsideFootprint = std::ranges::any_of(candidate->getHexes(), [&](const BattleHex & hex)
			{
				return hex.isValid() && !battle.battleIsInsideWalls(hex);
			});
			const bool hasInsideFootprint = std::ranges::any_of(candidate->getHexes(), [&](const BattleHex & hex)
			{
				return hex.isValid() && battle.battleIsInsideWalls(hex);
			});

			// Count only a controlled allied ground melee force outside the walls;
			// neither the acting Cyclops itself, shooters, siege engines, nor a
			// Puppet-controlled unit on the opposing side creates a breach need.
			if(candidateOwnerSide == actionSide && candidateControllerSide == actionSide
				&& !candidate->hasBonusOfType(BonusType::FLYING) && !candidate->isShooter()
				&& !candidate->hasBonusOfType(BonusType::SIEGE_WEAPON) && candidate->isMeleeAttacker()
				&& hasOutsideFootprint)
				alliedGroundForceOutside = true;

			const auto hostileSide = CBattleInfoEssentials::otherSide(actionSide);
			if(candidateOwnerSide == hostileSide && candidateControllerSide == hostileSide
				&& hasInsideFootprint)
				hostileStackInside = true;
		}

		if(alliedGroundForceOutside && hostileStackInside)
			return true;
	}

	// Otherwise preserve ordinary valuable attacks and use an available wall
	// shot only when the evaluator found no attack, or selected movement/defense.
	return ordinaryAction.actionType == EActionType::DEFEND
		|| ordinaryAction.actionType == EActionType::WALK;
}

BattleAction CBattleAI::useCatapult(const BattleID & battleID, const CStack * stack)
{
	BattleAction attack;
	BattleHex targetHex = BattleHex::INVALID;
	const auto battle = cb->getBattle(battleID);
	const auto currentControllerSide = battle->playerToSide(battle->battleGetActionController(stack));

	if(battle->battleGetGateState() == EGateState::CLOSED)
	{
		const auto gateHex = battle->wallPartToBattleHex(EWallPart::GATE);
		if(gateHex.isValid() && battle->isWallPartAttackable(EWallPart::GATE))
			targetHex = gateHex;
	}
	if(!targetHex.isValid())
	{
		std::array wallParts {
			EWallPart::KEEP,
			EWallPart::BOTTOM_TOWER,
			EWallPart::UPPER_TOWER,
			EWallPart::BELOW_GATE,
			EWallPart::OVER_GATE,
			EWallPart::BOTTOM_WALL,
			EWallPart::UPPER_WALL
		};

		const auto structuralOutput = std::max<int32_t>(0, battle->battleGetCatapultStructuralDamage(stack, 1));
		int32_t bestDamage = -1;
		for(auto wallPart : wallParts)
		{
			if(!battle->isWallPartAttackable(wallPart))
				continue;

			const auto candidateHex = battle->wallPartToBattleHex(wallPart);
			if(!candidateHex.isValid())
				continue;

			const auto wallHp = std::max<int32_t>(0, battle->getWallStructuralHP(wallPart));
			auto predictedDamage = wallHp > 0 && structuralOutput > 0
				? std::min(wallHp, structuralOutput) : 0;
			const auto overflow = battle->battleGetBreachmakerPreview(stack, wallPart, structuralOutput);
			if(overflow.damage > 0)
				predictedDamage += std::min(overflow.damage, battle->getWallStructuralHP(overflow.part));
			if(!targetHex.isValid() || predictedDamage > bestDamage)
			{
				targetHex = candidateHex;
				bestDamage = predictedDamage;
			}
		}
	}

	if(!targetHex.isValid())
	{
		auto defend = BattleAction::makeDefend(stack);
		defend.side = stack->unitSide();
		return defend;
	}

	attack.aimToHex(targetHex);
	attack.actionType = EActionType::CATAPULT;
	attack.side = stack->unitSide();
	attack.stackNumber = stack->unitId();

	movesSkippedByDefense = 0;

	return attack;
}

void CBattleAI::battleStart(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide Side, bool replayAllowed)
{
	timingState.store(std::make_shared<TimingState>(battleID, playerID));
	LOG_TRACE(logAi);
	side = Side;
	auto tacticsSettings = TacticsHandler::Settings{.enabled = autobattlePreferences.enableTacticsUsage};
	tacticsHandler = std::make_unique<TacticsHandler>(cb, battleID, tacticsSettings);
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

void CBattleAI::print(const std::string &text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}

std::optional<BattleAction> CBattleAI::considerFleeingOrSurrendering(const BattleID & battleID)
{
	BattleStateInfoForRetreat bs;

	bs.canFlee = cb->getBattle(battleID)->battleCanFlee();
	bs.canSurrender = cb->getBattle(battleID)->battleCanSurrender(playerID);
	bs.ourSide = cb->getBattle(battleID)->battleGetMySide();
	bs.ourHero = cb->getBattle(battleID)->battleGetMyHero();
	bs.enemyHero = nullptr;

	for(auto stack : cb->getBattle(battleID)->battleGetAllStacks(false))
	{
		if(stack->alive())
		{
			if(stack->unitSide() == bs.ourSide)
				bs.ourStacks.push_back(stack);
			else
			{
				bs.enemyStacks.push_back(stack);
				bs.enemyHero = cb->getBattle(battleID)->battleGetOwnerHero(stack);
			}
		}
	}

	bs.turnsSkippedByDefense = movesSkippedByDefense / bs.ourStacks.size();

	if(!bs.canFlee && !bs.canSurrender)
	{
		return std::nullopt;
	}

	auto result = cb->makeSurrenderRetreatDecision(battleID, bs);

	if(!result && bs.canFlee && bs.turnsSkippedByDefense > 30)
	{
		return BattleAction::makeRetreat(bs.ourSide);
	}

	return result;
}
