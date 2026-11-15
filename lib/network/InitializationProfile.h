/* New Horizons opt-in initialization measurements. GPL-2.0-or-later. */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class JsonNode;

namespace nhLoadProfile
{
enum class Metric : size_t
{
	WorldInit, InitHeroes, HeroCapture, HeroResolve, NativePrepare, PerkValidation,
	ProfileValidation, PerkDefinition, StartingPerks, ReplayRecord, SnapshotWrite,
	SnapshotRead, WorldWrite, WorldRead, LobbyWrite, LobbyRead, ClientVisit, JsonSizing,
	OnMapHeroes, PoolHeroes, RankBasic, RankAdvanced, RankExpert, ProfileRows,
	CapturedPerkBytes, CapturedDevelopmentBytes, SnapshotBytes, SkillRows, PerkRows, ProfileChoices, Count
};
struct Sample
{
	uint64_t calls = 0;
	uint64_t inclusiveNs = 0;
	uint64_t exclusiveNs = 0;
	uint64_t value = 0;
};
// Explicit timestamps make nesting/overflow testable without sleeping or a game.
class DLL_LINKAGE Accumulator
{
	struct Frame
	{
		Metric metric;
		uint64_t start;
		uint64_t children;
	};
	std::array<Frame, 64> stack{};
	size_t depth = 0;
public:
	std::array<Sample, static_cast<size_t>(Metric::Count)> samples{};
	uint64_t dropped = 0;
	bool enter(Metric metric, uint64_t now) noexcept;
	void leave(uint64_t now) noexcept;
	bool empty() const noexcept { return depth == 0; }
};
#ifdef NH_PERF_EXPERIMENTS
bool DLL_LINKAGE active() noexcept;
void DLL_LINKAGE count(Metric metric, uint64_t value) noexcept;
void DLL_LINKAGE jsonBytes(Metric metric, const JsonNode & node) noexcept;
class DLL_LINKAGE Phase
{
	bool owner = false;
	const char * label;
public:
	explicit Phase(const char * label) noexcept;
	~Phase();
	Phase(const Phase &) = delete;
	Phase & operator=(const Phase &) = delete;
};
class DLL_LINKAGE Scope
{
	bool entered = false;
public:
	explicit Scope(Metric metric) noexcept;
	~Scope();
	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;
};
#else
inline bool active() noexcept { return false; }
inline void count(Metric, uint64_t) noexcept {}
inline void jsonBytes(Metric, const JsonNode &) noexcept {}
class Phase { public: explicit Phase(const char *) noexcept {} };
class Scope { public: explicit Scope(Metric) noexcept {} };
#endif
// Read the already initialized ranks, never invoke a gameplay query for metrics.
template<typename Entries> void ranks(const Entries & entries) noexcept
{
#ifdef NH_PERF_EXPERIMENTS
	if(active())
		for(const auto & entry : entries)
			switch(entry.second)
			{
				case 1: count(Metric::RankBasic, 1); break;
				case 2: count(Metric::RankAdvanced, 1); break;
				case 3: count(Metric::RankExpert, 1); break;
				default: break;
			}
#else
	(void)entries;
#endif
}
}
