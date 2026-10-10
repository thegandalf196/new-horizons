/*
 * NewHorizonsWarMachines.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "SiegeInfo.h"
#include <array>
#include <cstdint>

struct DLL_LINKAGE FirstAidStructureRepairPreview
{
	EWallPart part = EWallPart::INVALID;
	int32_t expectedHP = 0;
	int32_t replacementHP = 0;
	int32_t repairedHP() const { return replacementHP - expectedHP; }
};

struct DLL_LINKAGE BreachmakerPreview
{
	EWallPart part = EWallPart::INVALID;
	int32_t damage = 0;
};

namespace newHorizonsWarMachines
{
// Physical outer-wall adjacency, deliberately independent of the enum layout.
inline constexpr std::array OUTER_PARTS{
	EWallPart::BOTTOM_TOWER, EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE,
	EWallPart::GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL, EWallPart::UPPER_TOWER};

template<typename HealthQuery>
BreachmakerPreview overflow(EWallPart struck, int32_t beforeHP, int32_t damage, HealthQuery health)
{
	if(struck == EWallPart::BOTTOM_TOWER || struck == EWallPart::UPPER_TOWER
		|| struck == EWallPart::KEEP || beforeHP <= 0 || damage <= beforeHP)
		return {};
	const auto carry = (damage - beforeHP) / 2;
	if(carry <= 0)
		return {};
	for(size_t i = 0; i < OUTER_PARTS.size(); ++i)
	{
		if(OUTER_PARTS[i] != struck)
			continue;
		BreachmakerPreview result;
		int32_t lowest = 0;
		for(const auto index : {static_cast<int>(i) - 1, static_cast<int>(i) + 1})
		{
			if(index < 0 || index >= static_cast<int>(OUTER_PARTS.size()))
				continue;
			const auto part = OUTER_PARTS[index];
			const auto hp = health(part);
			if(hp > 0 && (lowest == 0 || hp < lowest))
			{
				lowest = hp;
				result = {part, carry};
			}
		}
		return result;
	}
	return {};
}
}
