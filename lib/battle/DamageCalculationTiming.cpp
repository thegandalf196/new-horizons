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
	nativeAttackContext.accumulate(other.nativeAttackContext);
	nativePhysicalModifiers.accumulate(other.nativePhysicalModifiers);
	nativeHeroOrders.accumulate(other.nativeHeroOrders);
	nativeFinalization.accumulate(other.nativeFinalization);
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
	if(phase == Phase::NATIVE_PAYLOAD)
	{
		nativeSample = current;
		nativeStage = &current->nativeAttackContext;
	}
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
	const auto boundary = now();
	finishNativeRegion(boundary);
	const auto elapsed = boundary - started;
	++stage->calls;
	stage->nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
	--activeStages;
	stage = nullptr;
	nativeStage = nullptr;
	nativeSample = nullptr;
}

void Scope::finishNativeRegion(Clock::time_point boundary)
{
	if(!nativeStage)
		return;
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(boundary - started).count();
	++nativeStage->calls;
	// Difference of cumulative integer times keeps closure exact even if the
	// steady clock's native tick needs rounding when converted to nanoseconds.
	nativeStage->nanoseconds += elapsed - nativeElapsedNanoseconds;
	nativeElapsedNanoseconds = elapsed;
}

void Scope::nextNativeRegion(NativeRegion region)
{
	if(!nativeSample || !stage
		|| static_cast<uint8_t>(region) != static_cast<uint8_t>(activeNativeRegion) + 1)
		return;
	Stage * next = nullptr;
	switch(region)
	{
		case NativeRegion::PHYSICAL_MODIFIERS: next = &nativeSample->nativePhysicalModifiers; break;
		case NativeRegion::HERO_ORDERS: next = &nativeSample->nativeHeroOrders; break;
		case NativeRegion::FINALIZATION: next = &nativeSample->nativeFinalization; break;
		default: return;
	}
	const auto boundary = now();
	finishNativeRegion(boundary);
	nativeStage = next;
	activeNativeRegion = region;
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
