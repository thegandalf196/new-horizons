/*
 * BattleStackColorPresentation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include "../../lib/Color.h"
#include <span>
#include <string_view>

class CStack;
class CSpell;

struct BattleStackFilterEffect
{
	const CStack * target;
	const CSpell * source;
	ColorRGBA effectColor;
	uint8_t transparency;
	bool persistent;
};

namespace battleStackColorPresentation
{
struct Appearance
{
	ColorRGBA color;
	uint8_t transparency = 255;
};

/// Identity tint is presentation only: it never applies Frozen or changes the
/// animation group. Spell filters retain their application order and precedence.
inline Appearance appearance(const CStack * stack, std::string_view creatureKey,
	bool physicallyFrozen, std::span<const BattleStackFilterEffect> filters)
{
	const ColorRGBA blue(114, 186, 218, 140);
	Appearance result{creatureKey == "new-horizons:livingArmor" ? blue : ColorRGBA(0, 0, 0, 0)};
	for(const auto & filter : filters)
	{
		if(filter.target == stack)
		{
			result.color = filter.effectColor;
			result.transparency = static_cast<int>(filter.transparency) * result.transparency / 255;
		}
	}
	if(physicallyFrozen)
		result.color = blue;
	return result;
}
}
