/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
namespace battle::residualLife
{
struct Transition
{
	int64_t accumulatedDamage = 0;
	int32_t grant = 0;
};
/// Accumulate actual creature HP loss, retaining fractional progress across casts.
DLL_LINKAGE Transition record(int64_t previous, int64_t damage, int32_t threshold, int32_t limit);
}
