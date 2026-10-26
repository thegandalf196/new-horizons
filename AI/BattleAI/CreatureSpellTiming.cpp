/*
 * CreatureSpellTiming.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CreatureSpellTiming.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string_view>

namespace creatureSpellTiming
{
namespace
{
uint64_t children(const Sample & sample)
{
	return sample.baselineSetup.totalMicroseconds + sample.baseline.total.totalMicroseconds
		+ sample.outcomeSetup.totalMicroseconds + sample.outcomeCast.totalMicroseconds
		+ sample.outcome.total.totalMicroseconds + sample.outcomeHealing.totalMicroseconds
		+ sample.ordinaryCast.totalMicroseconds;
}

uint64_t children(const Pressure & pressure)
{
	return pressure.cache.totalMicroseconds + pressure.targets.totalMicroseconds;
}

void append(std::string & result, const char * name, const Stage & stage)
{
	const std::string prefix = std::string(" ") + name;
	result += prefix + "_calls=" + std::to_string(stage.calls)
		+ prefix + "_us=" + std::to_string(stage.totalMicroseconds)
		+ prefix + "_max_us=" + std::to_string(stage.maxMicroseconds);
}

void append(std::string & result, const char * name, const Pressure & pressure)
{
	append(result, name, pressure.total);
	const std::string prefix(name);
	append(result, (prefix + "_cache").c_str(), pressure.cache);
	append(result, (prefix + "_targets").c_str(), pressure.targets);
	result += " " + prefix + "_units=" + std::to_string(pressure.eligibleUnits)
		+ " " + prefix + "_remainder_us=" + std::to_string(pressure.total.totalMicroseconds
			- std::min(pressure.total.totalMicroseconds, children(pressure)));
}
}

Admission Budget::take(bool enabled, unsigned int limit)
{
	if(!enabled)
		return {};
	const auto attempt = attempts.fetch_add(1, std::memory_order_relaxed);
	return {attempt + 1, attempt < limit, attempt == limit};
}

bool accountingValid(const Sample & sample)
{
	return children(sample) <= sample.total.totalMicroseconds
		&& children(sample.baseline) <= sample.baseline.total.totalMicroseconds
		&& children(sample.outcome) <= sample.outcome.total.totalMicroseconds;
}

std::string fields(const Sample & sample)
{
	std::string result = "battle=" + std::to_string(sample.battle)
		+ " player=" + std::to_string(sample.player) + " side=" + std::to_string(sample.side)
		+ " round=" + std::to_string(sample.round) + " unit=" + std::to_string(sample.unit)
		+ " creature=" + std::to_string(sample.creature) + " branch="
		+ (sample.randomPool ? "random_pool" : "ordinary")
		+ " selected=" + std::to_string(sample.selected)
		+ " representative_spell=" + std::to_string(sample.representativeSpell)
		+ " selected_recipient=" + std::to_string(sample.recipient)
		+ " selected_value_bits=" + std::to_string(sample.selectedValueBits)
		+ " threw=" + std::to_string(sample.threw)
		+ " recipients=" + std::to_string(sample.recipients)
		+ " pools_nonempty=" + std::to_string(sample.nonemptyPools)
		+ " outcomes=" + std::to_string(sample.outcomes)
		+ " ordinary_spells=" + std::to_string(sample.ordinarySpells)
		+ " ordinary_targets=" + std::to_string(sample.ordinaryTargets);
	for(const auto & [name, member] : {
		std::pair{"total", &Sample::total}, std::pair{"baseline_setup", &Sample::baselineSetup},
		std::pair{"outcome_setup", &Sample::outcomeSetup}, std::pair{"outcome_cast", &Sample::outcomeCast},
		std::pair{"outcome_healing", &Sample::outcomeHealing}, std::pair{"ordinary_cast", &Sample::ordinaryCast}})
		append(result, name, sample.*member);
	append(result, "baseline_pressure", sample.baseline);
	append(result, "outcome_pressure", sample.outcome);
	result += " remainder_us=" + std::to_string(sample.total.totalMicroseconds
		- std::min(sample.total.totalMicroseconds, children(sample)))
		+ " accounting_valid=" + std::to_string(accountingValid(sample));
	return result;
}

Call::Call(Sample * supplied, Now now) : sample(supplied), now(now)
{
	if(!sample)
	{
		static const bool optedIn = []
		{
			const char * value = std::getenv("NH_RENDER_LOAD_TIMINGS");
			return value && std::string_view(value) == "1";
		}();
		static Budget budget;
		static const auto limit = heroActionTiming::parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT"));
		const auto admission = budget.take(optedIn && logGlobal->isDebugEnabled() && logAi->isDebugEnabled(), limit);
		if(admission.capReached)
			logAi->debug("NH_CREATURE_SPELL_TIMING cap=%u reached; subsequent caster attempts omitted", limit);
		if(admission.admitted)
		{
			sample = &owned;
			ordinal = admission.ordinal;
			report = true;
		}
	}
	if(sample)
	{
		exceptions = std::uncaught_exceptions();
		started = now();
	}
}

Call::~Call()
{
	if(!sample)
		return;
	const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		now() - started).count());
	sample->total.accumulate({1, elapsed, elapsed});
	sample->threw = std::uncaught_exceptions() > exceptions;
	if(!report)
		return;
	try
	{
		logAi->debug("NH_CREATURE_SPELL_TIMING %s caster_attempt=%llu", fields(*sample),
			static_cast<unsigned long long>(ordinal));
	}
	catch(...)
	{
		// Local diagnostics must not replace the original action/result exception.
	}
}
}
