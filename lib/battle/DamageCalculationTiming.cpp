/*
 * DamageCalculationTiming.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "DamageCalculationTiming.h"

namespace damageCalculationTiming
{
namespace
{
thread_local Sample * current = nullptr;
thread_local Now currentNow = &Clock::now;
thread_local uint32_t activeStages = 0;
}

void Stage::accumulate(const Stage & other)
{
	calls += other.calls;
	nanoseconds += other.nanoseconds;
}

void Sample::accumulate(const Sample & other)
{
	nativePayload.accumulate(other.nativePayload);
	bonusPreparation.accumulate(other.bonusPreparation);
	luaCall.accumulate(other.luaCall);
	initialCalls += other.initialCalls;
	luckySecondCalls += other.luckySecondCalls;
	unluckySecondCalls += other.unluckySecondCalls;
	suppressedStages += other.suppressedStages;
}

Binding::Binding(Sample * receiver, Now now)
	: previous(current), previousNow(currentNow), installed(receiver && !activeStages)
{
	if(installed)
	{
		current = receiver;
		currentNow = now;
	}
}

Binding::~Binding()
{
	if(installed)
	{
		current = previous;
		currentNow = previousNow;
	}
}

Scope::Scope(Phase phase)
{
	if(!current)
		return;
	if(activeStages)
	{
		++current->suppressedStages;
		return;
	}
	switch(phase)
	{
		case Phase::NATIVE_PAYLOAD: stage = &current->nativePayload; break;
		case Phase::BONUS_PREPARATION: stage = &current->bonusPreparation; break;
		case Phase::LUA_CALL: stage = &current->luaCall; break;
	}
	now = currentNow;
	started = now();
	++activeStages;
}

Scope::~Scope()
{
	finish();
}

void Scope::finish()
{
	if(!stage)
		return;
	const auto elapsed = now() - started;
	++stage->calls;
	stage->nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
	--activeStages;
	stage = nullptr;
}

void noteCalculation(Calculation calculation)
{
	if(!current || activeStages)
		return;
	switch(calculation)
	{
		case Calculation::INITIAL: ++current->initialCalls; break;
		case Calculation::LUCKY_SECOND: ++current->luckySecondCalls; break;
		case Calculation::UNLUCKY_SECOND: ++current->unluckySecondCalls; break;
	}
}
}
