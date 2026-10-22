/*
 * DamageCalculationTiming.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <chrono>
#include <cstdint>

/// Optional call-local diagnostics, never gameplay state or serialized data.
namespace damageCalculationTiming
{
using Clock = std::chrono::steady_clock;
using Now = Clock::time_point (*)();
enum class Phase : uint8_t { NATIVE_PAYLOAD, BONUS_PREPARATION, LUA_CALL };
enum class Calculation : uint8_t { INITIAL, LUCKY_SECOND, UNLUCKY_SECOND };
enum class NativeRegion : uint8_t { ATTACK_CONTEXT, PHYSICAL_MODIFIERS, HERO_ORDERS, FINALIZATION };

struct DLL_LINKAGE Stage
{
	uint64_t calls = 0;
	uint64_t nanoseconds = 0;
	void accumulate(const Stage & other);
};

struct DLL_LINKAGE Sample
{
	Stage nativePayload;
	/// Sequential nested wall-time breakdown, sharing parent boundary readings.
	Stage nativeAttackContext;
	Stage nativePhysicalModifiers;
	Stage nativeHeroOrders;
	Stage nativeFinalization;
	Stage bonusPreparation;
	/// Inclusive bridge input/output conversion, Lua, and native callbacks.
	Stage luaCall;
	uint64_t initialCalls = 0;
	uint64_t luckySecondCalls = 0;
	uint64_t unluckySecondCalls = 0;
	uint64_t suppressedStages = 0;
	void accumulate(const Sample & other);
};

/// Bound only by an explicitly admitted cache diagnostic phase. Thread-local
/// routing borrows its sink until this scope returns; no task propagation.
class DLL_LINKAGE Binding
{
	Sample * previous;
	Now previousNow;
	bool installed;
public:
	explicit Binding(Sample * receiver, Now now = &Clock::now);
	~Binding();
	Binding(const Binding &) = delete;
	Binding & operator=(const Binding &) = delete;
};

/// Sequential regions within dynamic damage, not additions to its wall time.
/// Nested callback calculations remain included in the outer region and are
/// suppressed here, rather than counted a second time.
class DLL_LINKAGE Scope
{
	Stage * stage = nullptr;
	Now now = nullptr;
	Clock::time_point started;
	Sample * nativeSample = nullptr;
	Stage * nativeStage = nullptr;
	NativeRegion activeNativeRegion = NativeRegion::ATTACK_CONTEXT;
	int64_t nativeElapsedNanoseconds = 0;
	void finishNativeRegion(Clock::time_point boundary);
public:
	explicit Scope(Phase phase);
	~Scope();
	void finish();
	/// Only the next sequential region of an admitted native scope can advance.
	/// Disabled, suppressed, other-phase, finished or out-of-order calls do nothing.
	void nextNativeRegion(NativeRegion region);
	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;
};

DLL_LINKAGE void noteCalculation(Calculation calculation);
}
