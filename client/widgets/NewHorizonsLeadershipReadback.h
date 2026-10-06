/*
 * NewHorizonsLeadershipReadback.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace newHorizonsLeadershipReadback
{
struct Proposal
{
	int64_t leadership = 0;
	int64_t requirement = 0;
	int64_t maximumCount = 0;
	int64_t existingCount = 0;
	int64_t incomingCount = 0;
	int64_t legalIncomingCount = 0;
	int64_t resultingCount = 0;
	int64_t addedLeadership = 0;
	int64_t resultingDemand = 0;
	int64_t excess = 0;

	bool operator==(const Proposal &) const = default;
};

namespace detail
{
constexpr int64_t nonNegative(int64_t value)
{
	return std::max<int64_t>(0, value);
}

constexpr int64_t saturatedAdd(int64_t left, int64_t right)
{
	const auto maximum = std::numeric_limits<int64_t>::max();
	return right > maximum - left ? maximum : left + right;
}

constexpr int64_t saturatedMultiply(int64_t left, int64_t right)
{
	const auto maximum = std::numeric_limits<int64_t>::max();
	return left != 0 && right > maximum / left ? maximum : left * right;
}
}

/// Read-only per-creature-slot demand for a proposed recruitment or transfer.
/// `incomingCount` is the requested proposal; `legalIncomingCount` reports
/// the portion that fits without changing the existing authoritative limits.
inline Proposal evaluate(const newHorizonsHeroes::LeadershipSlotCapacity & capacity,
	int64_t existingCount, int64_t incomingCount)
{
	Proposal result;
	result.leadership = detail::nonNegative(capacity.leadership);
	result.requirement = detail::nonNegative(capacity.requirement);
	result.maximumCount = detail::nonNegative(capacity.maximum);
	result.existingCount = detail::nonNegative(existingCount);
	result.incomingCount = detail::nonNegative(incomingCount);
	result.legalIncomingCount = std::min(result.incomingCount,
		std::max<int64_t>(0, result.maximumCount - std::min(result.maximumCount, result.existingCount)));
	result.resultingCount = detail::saturatedAdd(result.existingCount, result.incomingCount);
	result.addedLeadership = detail::saturatedMultiply(result.incomingCount, result.requirement);
	result.resultingDemand = detail::saturatedMultiply(result.resultingCount, result.requirement);
	result.excess = result.resultingDemand > result.leadership
		? result.resultingDemand - result.leadership
		: 0;
	return result;
}
}
