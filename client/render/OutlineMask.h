/*
 * OutlineMask.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>

namespace outlineMask
{
/// Threshold zero preserves legacy fringe-alpha outlines; positive thresholds
/// select a solid silhouette in the overlay only, without editing sprite RGBA.
template<typename AlphaReader>
uint8_t pixelAlpha(int x, int y, int width, int height, int thickness, uint8_t threshold, uint8_t colorAlpha, AlphaReader getAlpha)
{
	const uint8_t alpha = getAlpha(x, y);
	if(threshold == 0 ? alpha != 0 : alpha >= threshold)
		return 0;

	uint8_t maximum = 0;
	for(int dy = -thickness; dy <= thickness; ++dy)
	{
		for(int dx = -thickness; dx <= thickness; ++dx)
		{
			if(dx * dx + dy * dy > thickness * thickness)
				continue;
			const int nx = x + dx;
			const int ny = y + dy;
			if(nx < 0 || ny < 0 || nx >= width || ny >= height)
				continue;
			maximum = std::max(maximum, getAlpha(nx, ny));
		}
	}
	if(threshold == 0)
		return maximum;
	return maximum >= threshold ? colorAlpha : 0;
}
}
