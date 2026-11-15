/* New Horizons opt-in initialization measurements. GPL-2.0-or-later. */
#include "StdInc.h"
#include "InitializationProfile.h"
#include "PerformanceTrace.h"
#ifdef NH_PERF_EXPERIMENTS
#include "../json/JsonNode.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iterator>
#endif

namespace nhLoadProfile
{
bool Accumulator::enter(Metric metric, uint64_t now) noexcept
{
	if(depth == stack.size() || metric >= Metric::Count)
	{
		++dropped;
		return false;
	}
	stack[depth++] = {metric, now, 0};
	return true;
}
void Accumulator::leave(uint64_t now) noexcept
{
	if(!depth)
	{
		++dropped;
		return;
	}
	const auto frame = stack[--depth];
	if(now < frame.start)
	{
		++dropped;
		return;
	}
	const uint64_t elapsed = now - frame.start;
	auto & sample = samples[static_cast<size_t>(frame.metric)];
	++sample.calls;
	sample.inclusiveNs += elapsed;
	if(frame.children > elapsed)
		++dropped;
	else
		sample.exclusiveNs += elapsed - frame.children;
	if(depth)
		stack[depth - 1].children += elapsed;
}
#ifdef NH_PERF_EXPERIMENTS
namespace
{
struct Storage
{
	Accumulator data;
	bool phase = false;
	uint64_t started = 0;
};
thread_local Storage storage;
std::atomic<unsigned> reports{0};
constexpr const char * names[] = {
	"world-init", "init-heroes", "hero-capture", "hero-resolve", "native-prepare", "perk-validation",
	"profile-validation", "perk-definition", "starting-perks", "replay-record", "snapshot-write",
	"snapshot-read", "world-write", "world-read", "lobby-write", "lobby-read", "client-visit", "json-sizing",
	"on-map-heroes", "pool-heroes", "rank-basic", "rank-advanced", "rank-expert", "profile-rows",
	"captured-perk-json-bytes", "captured-development-json-bytes", "snapshot-bytes",
	"skill-rows", "perk-rows", "profile-choices"};
static_assert(std::size(names) == static_cast<size_t>(Metric::Count));
bool environment(const char * name) noexcept
{
	const char * value = std::getenv(name);
	return value && std::strcmp(value, "1") == 0;
}
bool enabled() noexcept
{
	static const bool result = environment("NH_MAPLOAD_PROFILE");
	return result;
}
void report(const char * label)
{
	const auto & data = storage.data;
	bool interesting = false;
	for(auto metric : {Metric::WorldInit, Metric::WorldWrite, Metric::WorldRead, Metric::LobbyWrite, Metric::LobbyRead, Metric::SnapshotWrite, Metric::SnapshotRead})
		interesting |= data.samples[static_cast<size_t>(metric)].calls != 0;
	if(!interesting)
		return;
	const auto reportId = reports.fetch_add(1);
	if(reportId >= 32)
	{
		if(reportId == 32)
			logGlobal->warn("NH_LOAD_PROFILE overflow report_budget=32 subsequent_phases_omitted=1");
		return;
	}
	logGlobal->info("NH_LOAD_PROFILE report=%u phase=%s start_ns=%llu end_ns=%llu valid=%d dropped=%llu json_volume_pass=%d",
		reportId, label, static_cast<unsigned long long>(storage.started),
		static_cast<unsigned long long>(nhperf::nowNs()),
		data.empty() && data.dropped == 0, static_cast<unsigned long long>(data.dropped), environment("NH_MAPLOAD_PROFILE_BYTES"));
	for(size_t i = 0; i < data.samples.size(); ++i)
	{
		const auto & s = data.samples[i];
		if(!s.calls && !s.value)
			continue;
		logGlobal->info("NH_LOAD_PROFILE report=%u phase=%s metric=%s calls=%llu inclusive_ns=%llu exclusive_ns=%llu value=%llu", reportId, label, names[i],
			static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.inclusiveNs),
			static_cast<unsigned long long>(s.exclusiveNs), static_cast<unsigned long long>(s.value));
	}
}
}
bool active() noexcept
{
	return enabled() && storage.phase;
}
void count(Metric metric, uint64_t value) noexcept
{
	if(active())
		storage.data.samples[static_cast<size_t>(metric)].value += value;
}
Phase::Phase(const char * name) noexcept : label(name)
{
	if(enabled() && !storage.phase)
	{
		storage.data = {};
		storage.phase = owner = true;
		storage.started = nhperf::nowNs();
	}
}
Phase::~Phase()
{
	if(!owner)
		return;
	storage.phase = false;
	try
	{
		report(label);
	}
	catch(...)
	{
		// Diagnostics never abort gameplay.
	}
}
Scope::Scope(Metric metric) noexcept
{
	if(active())
		entered = storage.data.enter(metric, nhperf::nowNs());
}
Scope::~Scope()
{
	if(entered)
		storage.data.leave(nhperf::nowNs());
}
void jsonBytes(Metric metric, const JsonNode & node) noexcept
{
	if(!active() || !environment("NH_MAPLOAD_PROFILE_BYTES"))
		return;
	Scope sizing(Metric::JsonSizing);
	try
	{
		count(metric, node.toString().size());
	}
	catch(...)
	{
		++storage.data.dropped;
	}
}
#endif
}
