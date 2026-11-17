/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastAcceptedSnapshotSeal.h"
#include "AttackPossibility.h"
#include <algorithm>
namespace forecast
{
namespace
{
template<typename Value> bool sameLease(const std::shared_ptr<Value> & left,
	const std::shared_ptr<Value> & right) noexcept
{
	return left.get() == right.get() && !left.owner_before(right) && !right.owner_before(left);
}
}
AcceptedSnapshotSeal::AcceptedSnapshotSeal(const AttackPossibility & action)
	: source(action.forecastSource), query(action.queryWorld), effects(action.effectPreview),
	actor(action.attackerState), recipients(action.affectedUnits) {}
bool AcceptedSnapshotSeal::matches(const AttackPossibility & action) const noexcept
{
	return source && query && sameLease(source, action.forecastSource) && sameLease(query, action.queryWorld)
		&& sameLease(effects, action.effectPreview) && sameLease(actor, action.attackerState)
		&& std::ranges::equal(recipients, action.affectedUnits,
			[](const auto & left, const auto & right) { return sameLease(left, right); });
}
bool AcceptedSnapshotSeal::contains(const battle::CUnitState & snapshot) const noexcept
{
	return actor.get() == &snapshot || std::ranges::any_of(recipients,
		[&](const auto & value) { return value.get() == &snapshot; });
}
}
