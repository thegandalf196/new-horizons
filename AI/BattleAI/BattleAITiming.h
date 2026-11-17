/*
 * BattleAITiming.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleAI.h"
#include "BattleEvaluator.h"
#include "ForecastNativeReadView.h"

#include <chrono>
#include <utility>
#include <type_traits>

/// Private, stack-owned callback timing; detached state may outlive the AI owner.
class CBattleAI::TimingScope
{
	using Clock = std::chrono::steady_clock;
	std::shared_ptr<TimingState> state;
	Clock::time_point started = Clock::now();
	TimingSummary stageTotals;
#ifdef NH_PERF_EXPERIMENTS
	battleSearch::RootSearchWorkCapture rootSearchWork;
	std::shared_ptr<forecast::NativeReadWork> nativeReadSink;
	bool nativeReadsCaptured = false;
#endif
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
	Clock::time_point decisionReady;
	Clock::time_point submitBegin;
	Clock::time_point submitEnd;

	class StageScope
	{
		StageTiming & timing;
		Clock::time_point started = Clock::now();
	public:
		explicit StageScope(StageTiming & timing) : timing(timing) {}
		~StageScope()
		{
			++timing.calls;
			timing.totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - started).count();
		}
	};
public:
#ifdef NH_PERF_EXPERIMENTS
	const std::shared_ptr<battleSearch::SearchBudget> & beginSearchDecision(battleSearch::Profile profile)
	{
		if(!nativeReadSink) nativeReadSink = std::make_shared<forecast::NativeReadWork>();
		return rootSearchWork.begin(profile, battleSearch::workLimits());
	}
	const auto & forecastWorkSink() const noexcept { return nativeReadSink; }
	void completeSearchDecision() noexcept
	{
		if(nativeReadSink && !nativeReadsCaptured)
		{
			stageTotals.forecast.add(&forecast::Counters::nativeReadMaterializations, nativeReadSink->materialized);
			stageTotals.forecast.add(&forecast::Counters::nativeOwningLoans, nativeReadSink->owningLoans);
			stageTotals.forecast.add(&forecast::Counters::nativeQueryRebindings, nativeReadSink->queryRebindings);
			stageTotals.forecast.saturated |= nativeReadSink->saturated;
			nativeReadsCaptured = true;
		}
		rootSearchWork.complete();
	}
#endif
	BattleEvaluator::SelectionTiming * selectionSink();

	/// The measured regions are non-nested within this callback. Stage data is
	/// local until callback exit, including when submission destroys the AI owner.
	template<typename Callable>
	decltype(auto) measure(StageTiming TimingSummary::* stage, Callable && action)
	{
		StageScope measurement(stageTotals.*stage);
		if constexpr(std::is_same_v<std::invoke_result_t<Callable>, BattleEvaluator>)
		{
			auto evaluator = std::forward<Callable>(action)();
			stageTotals.forecast.accumulate(evaluator.takeForecastPreparation());
			return evaluator;
		}
		else

#ifdef NH_PERF_EXPERIMENTS
		if constexpr(std::is_same_v<std::invoke_result_t<Callable>, bool>)
		{
			bool result = std::forward<Callable>(action)();
			// Successful Hero Action may have submitted internally. Its nested
			// forecast scopes have returned; detached timing owns the root safely.
			if(result && stage == &TimingSummary::heroAction) completeSearchDecision();
			return result;
		}
		else
#endif
		return std::forward<Callable>(action)();
	}

	void submitUnitAction(std::shared_ptr<CBattleCallback> callback,
		const BattleID & battleID, const BattleAction & action);
	TimingScope(std::shared_ptr<TimingState> current, const BattleID & battleID, const CStack * stack);
	~TimingScope();
};
