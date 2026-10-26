/*
 * CreatureSpellTiming.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HeroActionTiming.h"
#include <atomic>
#include <utility>

namespace creatureSpellTiming
{
using Stage = heroActionTiming::Stage;
using Scope = heroActionTiming::Scope;
using Now = heroActionTiming::Now;

struct Pressure
{
	Stage total;
	Stage cache;
	Stage targets;
	uint64_t eligibleUnits = 0;
};

struct DLL_LINKAGE Sample
{
	int battle = -1, player = -1, side = -1, round = -1;
	int64_t unit = -1, creature = -1;
	bool randomPool = false, selected = false, threw = false;
	int representativeSpell = -1;
	int64_t recipient = -1;
	uint32_t selectedValueBits = 0;
	uint64_t recipients = 0, nonemptyPools = 0, outcomes = 0;
	uint64_t ordinarySpells = 0, ordinaryTargets = 0;
	Stage total, baselineSetup, outcomeSetup, outcomeCast, outcomeHealing, ordinaryCast;
	Pressure baseline, outcome;
};

struct Admission
{
	uint64_t ordinal = 0;
	bool admitted = false, capReached = false;
};

class DLL_LINKAGE Budget
{
	std::atomic<uint64_t> attempts{0};
public:
	Admission take(bool enabled, unsigned int limit);
};

DLL_LINKAGE bool accountingValid(const Sample & sample);
DLL_LINKAGE std::string fields(const Sample & sample);

/// A supplied sink measures locally without runtime logging or process-budget use.
/// The default path uses the existing opt-in/debug gate; OFF reads no clock.
class DLL_LINKAGE Call
{
	Sample owned;
	Sample * sample;
	Now now;
	heroActionTiming::Clock::time_point started;
	int exceptions = 0;
	uint64_t ordinal = 0;
	bool report = false;
public:
	explicit Call(Sample * supplied = nullptr, Now now = &heroActionTiming::Clock::now);
	~Call();
	Call(const Call &) = delete;
	Call & operator=(const Call &) = delete;
	Sample * sink() { return sample; }
};

template<typename Callable>
decltype(auto) measure(Stage * stage, Callable && action)
{
	Scope measurement(stage);
	return std::forward<Callable>(action)();
}
}
