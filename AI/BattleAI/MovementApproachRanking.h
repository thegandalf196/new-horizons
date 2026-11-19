/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace movementApproachRanking
{
/// Approximate shortlist only: exact utility, bypasses and winning destination
/// remain the exchange evaluator's responsibility. Equal estimates retain the
/// admission order, including at the cutoff; selected entries execute in that
/// original order. This is deliberately independent of legality/admission.
inline std::vector<std::size_t> select(std::span<const float> estimates)
{
	constexpr std::size_t limit = 3;
	std::vector<std::size_t> selected(estimates.size());
	std::iota(selected.begin(), selected.end(), std::size_t{0});
	if(selected.size() <= limit)
		return selected;

	const auto value = [&](std::size_t index)
	{
		return std::isnan(estimates[index]) ? -std::numeric_limits<float>::infinity() : estimates[index];
	};
	std::partial_sort(selected.begin(), selected.begin() + limit, selected.end(), [&](std::size_t left, std::size_t right)
	{
		const float a = value(left);
		const float b = value(right);
		return a == b ? left < right : a > b;
	});
	selected.resize(limit);
	std::sort(selected.begin(), selected.end());
	return selected;
}
}
