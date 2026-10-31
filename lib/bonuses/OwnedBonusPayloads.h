/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Bonus.h"
#include <vector>
namespace ownedBonusPayloads
{
class Captured;
DLL_LINKAGE std::unique_ptr<Captured> copy(const std::vector<std::shared_ptr<const Bonus>> & roots);
/// Retain through every recipe and borrowed query result. Owns all nested values,
/// including mutable retirement handles for cycles below const parameter edges.
class DLL_LINKAGE Captured final
{
	friend std::unique_ptr<Captured> copy(const std::vector<std::shared_ptr<const Bonus>> & roots);
	std::vector<std::shared_ptr<Bonus>> bonuses;
	std::vector<std::shared_ptr<IUpdater>> updaters;
	std::vector<std::shared_ptr<ILimiter>> limiters;
	std::vector<std::shared_ptr<Bonus>> rootValues;
	Captured() = default;
public:
	~Captured();
	Captured(const Captured &) = delete;
	Captured & operator=(const Captured &) = delete;
	const std::vector<std::shared_ptr<Bonus>> & roots() const noexcept { return rootValues; }
};
}
