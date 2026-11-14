/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <algorithm>
#include <cmath>

namespace leadershipPresentation
{
enum class BadgeRole { BATTLE, PORTRAIT, MINIATURE };

struct BadgeSize
{
	int width;
	int height;
};

constexpr BadgeSize badgeSize(BadgeRole role)
{
	if(role == BadgeRole::PORTRAIT) return {34, 11};
	return {23, 9};
}

inline bool reducedEfficiency(double efficiency)
{
	return std::isfinite(efficiency) && efficiency >= 0.0 && efficiency < 1.0;
}

inline int compactPercent(double efficiency)
{
	if(!std::isfinite(efficiency)) return 100;
	if(efficiency <= 0.0) return 0;
	if(efficiency >= 1.0) return 100;
	return std::clamp(static_cast<int>(std::floor(efficiency * 100.0)), 0, 99);
}
}
