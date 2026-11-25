/*
 * UnitHotStats.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "UnitHotStatsQuery.h"
#include "../CStack.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <typeinfo>

namespace battle
{
namespace
{
constexpr size_t HOT_STAT_COUNT = static_cast<size_t>(HotStat::COUNT);

constexpr std::array<const char *, HOT_STAT_COUNT> HOT_STAT_NAMES = {
	"suppression level", "hypnotized", "invincible", "maximum health", "Focus Magic charges", "Frozen",
	"dynamic damage passive", "siege weapon"};

bool hotStatsEnvironmentIs(const char * name, std::string_view value)
{
	const char * actual = std::getenv(name);
	return actual && std::string_view(actual) == value;
}

UnitHotStats::Mode readHotStatsMode()
{
	if(hotStatsEnvironmentIs("NH_HOT_STATS_CHECK", "1"))
		return UnitHotStats::Mode::CHECK;
	return hotStatsEnvironmentIs("NH_HOT_STATS", "0") ? UnitHotStats::Mode::OFF : UnitHotStats::Mode::ON;
}

/// Read once per process, so a run never mixes modes.
UnitHotStats::Mode configuredHotStatsMode()
{
	static const UnitHotStats::Mode mode = readHotStatsMode();
	return mode;
}

/// Checker-mode evidence, reported once at exit; never touched otherwise.
struct HotStatsCheckerReport
{
	std::array<std::atomic<uint64_t>, HOT_STAT_COUNT> verified{};
	std::array<std::atomic<uint64_t>, HOT_STAT_COUNT> computed{};
	~HotStatsCheckerReport()
	{
		uint64_t reused = 0;
		uint64_t fresh = 0;
		for(size_t index = 0; index < HOT_STAT_COUNT; ++index)
		{
			reused += verified[index].load();
			fresh += computed[index].load();
		}
		std::fprintf(stderr, "NH_HOT_STATS_CHECK: %llu reused answers verified, 0 mismatches, %llu computed:",
			static_cast<unsigned long long>(reused), static_cast<unsigned long long>(fresh));
		for(size_t index = 0; index < HOT_STAT_COUNT; ++index)
			std::fprintf(stderr, " [%s %llu/%llu]", HOT_STAT_NAMES[index], static_cast<unsigned long long>(verified[index].load()),
				static_cast<unsigned long long>(computed[index].load()));
		std::fputc('\n', stderr);
	}
};

HotStatsCheckerReport & hotStatsCheckerReport()
{
	static HotStatsCheckerReport report;
	return report;
}
}

UnitHotStats::UnitHotStats() noexcept
	: currentMode(configuredHotStatsMode())
	, slots{}
{
}

UnitHotStats::UnitHotStats(const UnitHotStats &) noexcept
	: UnitHotStats()
{
}

UnitHotStats & UnitHotStats::operator=(const UnitHotStats &) noexcept
{
	clear();
	return *this;
}

void UnitHotStats::clear() const noexcept
{
	for(auto & slot : slots)
		slot.store(0, std::memory_order_release);
}

namespace hotStats
{
void verify(const Unit & unit, HotStat stat, uint32_t key, int32_t reused, int32_t fresh)
{
	const auto index = static_cast<size_t>(stat);
	if(reused == fresh)
	{
		hotStatsCheckerReport().verified[index].fetch_add(1, std::memory_order_relaxed);
		return;
	}
	logGlobal->error("Hot stat mismatch for unit %d (%s): %s reused %d under key %d, fresh %d",
		unit.unitId(), typeid(unit).name(), HOT_STAT_NAMES[index], reused, key, fresh);
	std::fprintf(stderr, "NH_HOT_STATS_CHECK mismatch: unit %u %s reused %d key %u fresh %d\n",
		unit.unitId(), HOT_STAT_NAMES[index], reused, key, fresh);
	std::abort();
}

void noteComputed(HotStat stat)
{
	hotStatsCheckerReport().computed[static_cast<size_t>(stat)].fetch_add(1, std::memory_order_relaxed);
}
}
}

battle::UnitHotStatsAccess CStack::hotStatsAccess() const
{
	// Exact native stacks only: every input of a slot is in this node's bonus
	// tree, whose version advances on each change (the BonusCache key). A
	// derived stack may answer from state outside it.
	if(nativeHotStats.mode() == battle::UnitHotStats::Mode::OFF || typeid(*this) != typeid(CStack))
		return {};
	return {&nativeHotStats, static_cast<uint32_t>(CBonusSystemNode::getTreeVersion())};
}
