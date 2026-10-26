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

#include <chrono>
#include <utility>

/// Private, stack-owned callback timing; detached state may outlive the AI owner.
class CBattleAI::TimingScope
{
	using Clock = std::chrono::steady_clock;
	std::shared_ptr<TimingState> state;
	Clock::time_point started = Clock::now();
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
	BattleEvaluator::SelectionTiming * selectionSink();

	/// The measured regions are non-nested within this callback. Stage data is
	/// local until callback exit, including when submission destroys the AI owner.
	template<typename Callable>
	decltype(auto) measure(StageTiming TimingSummary::* stage, Callable && action)
	{
		StageScope measurement(stageTotals.*stage);
		return std::forward<Callable>(action)();
	}

	void submitUnitAction(std::shared_ptr<CBattleCallback> callback,
		const BattleID & battleID, const BattleAction & action);
	TimingScope(std::shared_ptr<TimingState> current, const BattleID & battleID, const CStack * stack);
	~TimingScope();
};
