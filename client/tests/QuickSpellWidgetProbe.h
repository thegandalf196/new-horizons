/* Test-only instrumentation; never include in the shipped player. GPL-2.0-or-later. */
#pragma once
#include <atomic>
#include <cstdint>

namespace quickSpellWidgetProbe
{
inline std::atomic<std::uint64_t> preferenceWrites{0};
inline std::atomic<std::uint64_t> castDispatches{0};
inline void beforePreferenceWrite() { preferenceWrites.fetch_add(1, std::memory_order_relaxed); }
inline void beforeCastDispatch() { castDispatches.fetch_add(1, std::memory_order_relaxed); }
}
