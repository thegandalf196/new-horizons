/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitStateRevision.h"

#include <atomic>

namespace battle
{
uint64_t nextUnitStateRevision() noexcept
{
	// Only uniqueness and order matter; no other memory is published through it.
	static std::atomic<uint64_t> counter{0};
	return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}
}
