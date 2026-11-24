/*
 * SpellSchoolBadgeLayout.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/Point.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

/// Pure layout of the spell school badge: which schools have a medallion and
/// where the 18x18 medallions sit on a native 83x61 spell scroll.
namespace spellSchoolBadge
{
constexpr int SCROLL_WIDTH = 83;
constexpr int SCROLL_HEIGHT = 61;
constexpr int MEDALLION = 18;
/// Space between the two medallions of a two-school spell.
constexpr int GAP = 2;
/// How far a badge reaches below the scroll's bottom edge.
constexpr int OVERHANG = MEDALLION / 2;
/// A spell in more schools than this gets no badge.
constexpr size_t MAX_SCHOOLS = 2;

/// The six New Horizons schools; each has the medallion NH_school_badge_<key>.
constexpr std::array<std::string_view, 6> SCHOOLS = {"light", "nature", "sorcery", "havoc", "shadow", "chaos"};

/// The medallion key of a school identifier such as "new-horizons:light";
/// empty for the original four elements and unknown modded schools.
inline std::optional<std::string_view> schoolKey(std::string_view identifier)
{
	const auto colon = identifier.rfind(':');
	if(colon != std::string_view::npos)
		identifier.remove_prefix(colon + 1);
	for(const auto key : SCHOOLS)
		if(key == identifier)
			return key;
	return std::nullopt;
}

/// Top-left corners of the medallions relative to the scroll: one is centred
/// on the bottom edge, two sit side by side. Empty for no or too many schools.
inline std::vector<Point> medallionOrigins(size_t count)
{
	std::vector<Point> result;
	if(count == 0 || count > MAX_SCHOOLS)
		return result;
	const int medallions = static_cast<int>(count);
	const int left = (SCROLL_WIDTH - medallions * MEDALLION - (medallions - 1) * GAP) / 2;
	for(int index = 0; index < medallions; ++index)
		result.emplace_back(left + index * (MEDALLION + GAP), SCROLL_HEIGHT - MEDALLION / 2);
	return result;
}
}
