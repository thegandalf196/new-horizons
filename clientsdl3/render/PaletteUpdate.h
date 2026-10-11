/*
 * PaletteUpdate.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <SDL3/SDL_pixels.h>

namespace paletteUpdate
{
/// Identical writes still increment SDL3's palette version and invalidate textures.
inline void setColorIfChanged(SDL_Palette * palette, int index, const SDL_Color & color)
{
	const SDL_Color & current = palette->colors[index];
	if(current.r != color.r || current.g != color.g || current.b != color.b || current.a != color.a)
		SDL_SetPaletteColors(palette, &color, index, 1);
}
}
