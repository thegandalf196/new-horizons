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
#include <string_view>

namespace heroActionTiming
{
void Stage::accumulate(const Stage & other)
{
	calls += other.calls;
	totalMicroseconds += other.totalMicroseconds;
	maxMicroseconds = std::max(maxMicroseconds, other.maxMicroseconds);
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

void Candidates::finish(Summary & summary, bool threw) const
{
	for(const auto & slot : slots)
	{
		summary.continuation.full.accumulate(slot.full);
		summary.continuation.cached.accumulate(slot.cached);
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
			" submit_calls=%llu submit_us=%llu submit_max_us=%llu submit_returned=%llu submit_threw=%llu",
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
			static_cast<unsigned long long>(summary.submit.calls),
			static_cast<unsigned long long>(summary.submit.totalMicroseconds),
			static_cast<unsigned long long>(summary.submit.maxMicroseconds),
			static_cast<unsigned long long>(summary.submitReturned),
			static_cast<unsigned long long>(summary.submitThrew));
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
