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
	/// Concealment only: a second draw of the same frame with this colour
	/// filter at this transparency. Zero means no second draw.
	ColorRGBA overlayColor = ColorRGBA(0, 0, 0, 0);
	uint8_t overlayTransparency = 0;
};

/// How a concealed stack (Sylvan Tactics Ambush or Ivor's Forest Ambush) is
/// drawn: its own colours at reduced opacity, then a green recolour of the
/// same frame laid partway over them.
struct ConcealedLook
{
	uint8_t transparency;
	ColorRGBA overlayColor;
	uint8_t overlayStrength;
};

/// The owner-chosen concealed look: about 80% opacity, green layer at about 45%.
inline constexpr ConcealedLook CONCEALED_LOOK{204, ColorRGBA(120, 210, 100, 255), 115};

/// Identity tint is presentation only: it never applies Frozen or changes the
/// animation group. Spell filters retain their application order and precedence.
/// Concealment multiplies the transparency and keeps the creature's colours; its
/// green layer is drawn only while no spell colour or Frozen recolours the stack.
inline Appearance appearance(const CStack * stack, std::string_view creatureKey,
	bool physicallyFrozen, std::span<const BattleStackFilterEffect> filters,
	const ConcealedLook * concealed = nullptr)
{
	const ColorRGBA blue(114, 186, 218, 140);
	Appearance result{creatureKey == "new-horizons:livingArmor" ? blue : ColorRGBA(0, 0, 0, 0)};
	bool recoloured = false;
	if(concealed)
		result.transparency = concealed->transparency;
	for(const auto & filter : filters)
	{
		if(filter.target == stack)
		{
			result.color = filter.effectColor;
			result.transparency = static_cast<int>(filter.transparency) * result.transparency / 255;
			recoloured = recoloured || filter.effectColor.a != 0;
		}
	}
	if(physicallyFrozen)
		result.color = blue;
	if(concealed && !recoloured && !physicallyFrozen)
	{
		result.overlayColor = concealed->overlayColor;
		result.overlayTransparency = static_cast<int>(concealed->overlayStrength) * result.transparency / 255;
	}
	return result;
}
}
