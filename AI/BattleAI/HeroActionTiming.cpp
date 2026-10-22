/*
 * HeroActionTiming.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionTiming.h"
#include "../../lib/battle/Unit.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace heroActionTiming
{

namespace
{
using ExchangeTiming = BattleMovementTiming::ExchangeTiming;
using ExchangeStage = BattleMovementTiming::StageTiming;

// Existing body/probe regions only. Extra damage-cache clocks stay disabled;
// nested lifecycle subsets are not additional disjoint body time.
constexpr auto exchangeStages = {
	std::pair{"body", &ExchangeTiming::body}, std::pair{"membership", &ExchangeTiming::membership},
	std::pair{"projection_setup", &ExchangeTiming::projectionSetup}, std::pair{"target_probes", &ExchangeTiming::targetProbes},
	std::pair{"probe_preparation", &ExchangeTiming::targetProbePreparation},
	std::pair{"probe_main_damage", &ExchangeTiming::targetProbeMainDamage},
	std::pair{"probe_dps_score", &ExchangeTiming::targetProbeDpsScore},
	std::pair{"applied_attacks", &ExchangeTiming::appliedAttacks},
	std::pair{"activation_start", &ExchangeTiming::activationStart},
	std::pair{"activation_end", &ExchangeTiming::activationEnd}, std::pair{"round_advance", &ExchangeTiming::roundAdvance}};

void accumulateStage(ExchangeStage & total, const ExchangeStage & sample)
{
	total.calls += sample.calls;
	total.totalMicroseconds += sample.totalMicroseconds;
}

void accumulateExchange(ExchangeTiming & total, const ExchangeTiming & sample)
{
	for(const auto & [name, stage] : exchangeStages)
		accumulateStage(total.*stage, sample.*stage);
	total.remainderMicroseconds += sample.remainderMicroseconds;
}

void accumulateBestTarget(BattleBestTargetTiming & total, const BattleBestTargetTiming & sample)
{
	accumulateStage(total.waitedExchange, sample.waitedExchange);
	accumulateStage(total.normalExchange, sample.normalExchange);
	for(const auto member : {&BattleBestTargetTiming::waitedDetails, &BattleBestTargetTiming::normalDetails})
		accumulateExchange(total.*member, sample.*member);
}

void appendStage(std::string & fields, const std::string & prefix, const ExchangeStage & stage)
{
	fields += " " + prefix + "_calls=" + std::to_string(stage.calls);
	fields += " " + prefix + "_sum_us=" + std::to_string(stage.totalMicroseconds);
}

std::string bestTargetFields(const BattleBestTargetTiming & details)
{
	std::string fields;
	for(const auto & [name, member, wrapper] : {
		std::tuple{"waited", &BattleBestTargetTiming::waitedDetails, &BattleBestTargetTiming::waitedExchange},
		std::tuple{"normal", &BattleBestTargetTiming::normalDetails, &BattleBestTargetTiming::normalExchange}})
	{
		const std::string prefix = std::string("continuation_full_best_target_") + name;
		appendStage(fields, prefix + "_exchange", details.*wrapper);
		for(const auto & [stageName, stage] : exchangeStages)
			appendStage(fields, prefix + "_" + stageName, (details.*member).*stage);
		fields += " " + prefix + "_remainder_us=" + std::to_string((details.*member).remainderMicroseconds);
	}
	return fields;
}

std::string movementFields(const BattleMovementTiming & details)
{
	std::string fields;
	const std::string prefix = "continuation_full_movement";
	appendStage(fields, prefix + "_exchange", details.findMoveExchange);
	for(const auto & [name, stage] : exchangeStages)
		appendStage(fields, prefix + "_" + name, details.findMoveExchangeDetails.*stage);
	fields += " " + prefix + "_remainder_us=" + std::to_string(details.findMoveExchangeDetails.remainderMicroseconds);
	return fields;
}
}

void Stage::accumulate(const Stage & other)
{
	calls += other.calls;
	totalMicroseconds += other.totalMicroseconds;
	maxMicroseconds = std::max(maxMicroseconds, other.maxMicroseconds);
}

std::string typeFields(const TypeSummary & type)
{
	return " command=" + std::to_string(type.command) + " spell=" + std::to_string(type.spell)
		+ " overflow=" + std::to_string(type.command == -1 && type.spell == -1)
		+ " evaluations=" + std::to_string(type.candidate.calls)
		+ " sum_us=" + std::to_string(type.candidate.totalMicroseconds)
		+ " max_us=" + std::to_string(type.candidate.maxMicroseconds)
		+ " full_calls=" + std::to_string(type.full.calls)
		+ " full_sum_us=" + std::to_string(type.full.totalMicroseconds)
		+ " cached_calls=" + std::to_string(type.cached.calls)
		+ " cached_sum_us=" + std::to_string(type.cached.totalMicroseconds)
		+ " movement_sum_us=" + std::to_string(type.movement.totalMicroseconds)
		+ " best_target_sum_us=" + std::to_string(type.bestTarget.totalMicroseconds);
}

Scope::Scope(Stage * stage, Now now)
	: stage(stage), now(now), started(stage ? now() : Clock::time_point{})
{}

Scope::~Scope()
{
	if(!stage)
		return;
	const auto elapsed = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(now() - started).count());
	++stage->calls;
	stage->totalMicroseconds += elapsed;
	stage->maxMicroseconds = std::max(stage->maxMicroseconds, elapsed);
}

void Candidates::prepare(bool enabled, size_t count)
{
	if(enabled)
		slots.resize(count);
}

Continuation * Candidates::at(size_t index)
{
	return slots.empty() ? nullptr : &slots.at(index);
}

std::vector<TypeSummary> Candidates::types() const
{
	constexpr size_t maxTypes = 64;
	std::vector<TypeSummary> result;
	for(const auto & slot : slots)
	{
		if(!slot.candidate.calls)
			continue;
		auto found = std::find_if(result.begin(), result.end(), [&](const auto & type)
		{
			return type.command == slot.command && type.spell == slot.spell;
		});
		if(found == result.end())
		{
			if(result.size() < maxTypes)
			{
				result.emplace_back();
				result.back().command = slot.command;
				result.back().spell = slot.spell;
				found = result.end() - 1;
			}
			else
			{
				if(result.size() == maxTypes)
					result.emplace_back();
				found = result.begin() + maxTypes;
			}
		}
		found->candidate.accumulate(slot.candidate);
		found->full.accumulate(slot.full);
		found->cached.accumulate(slot.cached);
		found->movement.accumulate(slot.fullMovement);
		found->bestTarget.accumulate(slot.fullBestTarget);
	}
	return result;
}

void Candidates::finish(Summary & summary, bool threw) const
{
	for(const auto & slot : slots)
	{
		summary.continuation.full.accumulate(slot.full);
		summary.continuation.cached.accumulate(slot.cached);
		summary.continuation.fullSetup.accumulate(slot.fullSetup);
		summary.continuation.fullReachability.accumulate(slot.fullReachability);
		summary.continuation.fullMovement.accumulate(slot.fullMovement);
		summary.continuation.fullBestTarget.accumulate(slot.fullBestTarget);
		accumulateStage(summary.continuation.fullMovementDetails.findMoveExchange, slot.fullMovementDetails.findMoveExchange);
		accumulateExchange(summary.continuation.fullMovementDetails.findMoveExchangeDetails,
			slot.fullMovementDetails.findMoveExchangeDetails);
		accumulateBestTarget(summary.continuation.fullBestTargetDetails, slot.fullBestTargetDetails);
	}
	summary.candidates += slots.size();
	summary.loopJoined = true;
	summary.loopThrew = threw;
}

LoopScope::LoopScope(Candidates & candidates, Summary * summary, Now now)
	: candidates(candidates), summary(summary), exceptions(std::uncaught_exceptions()),
	wall(summary ? &summary->candidateLoop : nullptr, now)
{}

LoopScope::~LoopScope()
{
	// TBB's parallel_for returns/rethrows only after its task group has joined.
	// The wall member samples after this body, so loop_us also includes this reduction.
	if(summary)
		candidates.finish(*summary, std::uncaught_exceptions() > exceptions);
}

SubmitScope::SubmitScope(Summary * summary, Now now)
	: summary(summary), exceptions(std::uncaught_exceptions()),
	wall(summary ? &summary->submit : nullptr, now)
{}

SubmitScope::~SubmitScope()
{
	if(summary)
	{
		if(std::uncaught_exceptions() > exceptions)
			++summary->submitThrew;
		else
			++summary->submitReturned;
	}
}

unsigned int parseLimit(const char * value)
{
	if(!value || !*value)
		return 1024;
	unsigned int result = 0;
	for(const char * current = value; *current; ++current)
	{
		if(*current < '0' || *current > '9')
			return 1024;
		const auto digit = static_cast<unsigned int>(*current - '0');
		if(result > (16384u - digit) / 10)
			return 1024;
		result = result * 10 + digit;
	}
	return result ? result : 1024;
}

int64_t captureStackId(const battle::Unit * stack)
{
	return stack ? static_cast<int64_t>(stack->unitId()) : -1;
}

Call::Call()
{
	static const bool optedIn = []
	{
		const char * value = std::getenv("NH_RENDER_LOAD_TIMINGS");
		return value && std::string_view(value) == "1";
	}();
	if(!optedIn || !logGlobal->isDebugEnabled() || !logAi->isDebugEnabled())
		return;
	static const auto limit = parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT"));
	static std::atomic<uint64_t> attempts{0};
	const auto attempt = attempts.fetch_add(1, std::memory_order_relaxed);
	if(attempt == limit)
		logAi->debug("NH_HERO_ACTION_TIMING cap=%u reached; subsequent Hero attempt records omitted", limit);
	requested = attempt < limit;
	ordinal = attempt + 1;
}

Call::~Call()
{
	if(!requested)
		return;
	try
	{
		logAi->debug("NH_HERO_ACTION_TIMING battle=%d player=%d side=%d stack=%lld round=%d hero_attempt=%llu"
			" candidates=%llu loop_joined=%d loop_threw=%d loop_calls=%llu loop_us=%llu loop_max_us=%llu"
			" continuation_full_calls=%llu continuation_full_sum_us=%llu continuation_full_max_us=%llu"
			" continuation_cached_calls=%llu continuation_cached_sum_us=%llu continuation_cached_max_us=%llu"
			" continuation_full_setup_calls=%llu continuation_full_setup_sum_us=%llu continuation_full_setup_max_us=%llu"
			" continuation_full_reachability_calls=%llu continuation_full_reachability_sum_us=%llu continuation_full_reachability_max_us=%llu"
			" continuation_full_movement_calls=%llu continuation_full_movement_sum_us=%llu continuation_full_movement_max_us=%llu"
			" continuation_full_best_target_calls=%llu continuation_full_best_target_sum_us=%llu continuation_full_best_target_max_us=%llu"
			" submit_calls=%llu submit_us=%llu submit_max_us=%llu submit_returned=%llu submit_threw=%llu%s",
			identity.battle, identity.player, identity.side, static_cast<long long>(identity.stack), identity.round,
			static_cast<unsigned long long>(ordinal), static_cast<unsigned long long>(summary.candidates),
			summary.loopJoined, summary.loopThrew,
			static_cast<unsigned long long>(summary.candidateLoop.calls),
			static_cast<unsigned long long>(summary.candidateLoop.totalMicroseconds),
			static_cast<unsigned long long>(summary.candidateLoop.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.full.calls),
			static_cast<unsigned long long>(summary.continuation.full.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.full.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.cached.calls),
			static_cast<unsigned long long>(summary.continuation.cached.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.cached.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullSetup.calls),
			static_cast<unsigned long long>(summary.continuation.fullSetup.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullSetup.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullReachability.calls),
			static_cast<unsigned long long>(summary.continuation.fullReachability.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullReachability.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullMovement.calls),
			static_cast<unsigned long long>(summary.continuation.fullMovement.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullMovement.maxMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullBestTarget.calls),
			static_cast<unsigned long long>(summary.continuation.fullBestTarget.totalMicroseconds),
			static_cast<unsigned long long>(summary.continuation.fullBestTarget.maxMicroseconds),
			static_cast<unsigned long long>(summary.submit.calls),
			static_cast<unsigned long long>(summary.submit.totalMicroseconds),
			static_cast<unsigned long long>(summary.submit.maxMicroseconds),
			static_cast<unsigned long long>(summary.submitReturned),
			static_cast<unsigned long long>(summary.submitThrew),
			(bestTargetFields(summary.continuation.fullBestTargetDetails)
				+ movementFields(summary.continuation.fullMovementDetails)).c_str());
		// Same admitted attempt/budget as the aggregate; at most64 types + overflow.
		// Candidate sums may overlap with workers; never add children to totals.
		for(const auto & type : candidates.types())
			logAi->debug("NH_HERO_ACTION_TYPE_TIMING battle=%d player=%d side=%d stack=%lld round=%d hero_attempt=%llu"
				" loop_joined=%d loop_threw=%d%s",
				identity.battle, identity.player, identity.side, static_cast<long long>(identity.stack), identity.round,
				static_cast<unsigned long long>(ordinal), summary.loopJoined, summary.loopThrew,
				typeFields(type).c_str());
	}
	catch(...)
	{
		// Optional diagnostic output must not replace a gameplay exception.
	}
}

void Call::prepare(size_t count)
{
	candidates.prepare(requested, count);
}

Continuation * Call::continuation(size_t index)
{
	return requested ? candidates.at(index) : nullptr;
}
}
