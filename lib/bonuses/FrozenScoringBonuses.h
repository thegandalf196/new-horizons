/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Bonus.h"
#include <optional>
#include <span>
namespace frozenScoring
{
using Records = std::vector<std::shared_ptr<Bonus>>;
/// Materialized effective scoring answers. No originating raw/export authority.
struct View
{
	std::span<const Bonus> stacked;
	const std::optional<Records> & unstacked;
	bool veteranCohesionEarned;
};
/// Exact native frozen branch: select, clone stacked values, stack only that
/// selected stacked lane, then native Veteran Cohesion. No updater/limiter runs.
DLL_LINKAGE TConstBonusListPtr query(const View &, const CSelector &, bool unstacked);
}
