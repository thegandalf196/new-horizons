/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/spells/NewHorizonsMetamagicResolution.h"
#include <span>
#include <optional>
namespace metamagicAI
{
struct PoolExpectation
{
	double hitPoints = 0;
	double removals = 0;
};
/// Exact bounded independent-effect payout expectation, capped once per recipient.
/// Inputs must be eligible logical effects on one actually resolved recipient.
/// No callback, mutable battle, action search or random stream is consulted.
/// Returns unknown when the finite distribution exceeds the projection budget.
DLL_LINKAGE std::optional<PoolExpectation> expectedPool(
	std::span<const newHorizonsMetamagic::RemovalCandidate> effects,
	const newHorizonsMetamagic::ResolutionPolicy & policy, int rank, bool halon,
	newHorizonsMetamagic::ResolutionKind kind, int64_t capacity,
	const std::function<int64_t(int64_t)> & adjustCombinedPool = std::function<int64_t(int64_t)>());
}
