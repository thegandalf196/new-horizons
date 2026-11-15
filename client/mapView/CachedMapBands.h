/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/Rect.h"
#include <array>
#include <utility>

/// The wrapped terrain cache is contiguous in at most four rectangular bands.
/// Based on VCMI develop 948333932fdf91abda780906bf2d18eec9aa0fc1.
template<typename Visit>
void forEachCachedMapBand(const Rect & tiles, Point tileSize, Point cacheTileSize,
	Point origin, Visit && visit)
{
	if(tiles.w <= 0 || tiles.h <= 0)
		return;
	const int firstX = ((tiles.x % tiles.w) + tiles.w) % tiles.w;
	const int firstY = ((tiles.y % tiles.h) + tiles.h) % tiles.h;
	const std::array<std::pair<int, int>, 2> columns{{{firstX, tiles.w - firstX}, {0, firstX}}};
	const std::array<std::pair<int, int>, 2> rows{{{firstY, tiles.h - firstY}, {0, firstY}}};
	int offsetX = 0;
	for(const auto & column : columns)
	{
		int offsetY = 0;
		for(const auto & row : rows)
		{
			if(column.second > 0 && row.second > 0)
				visit(Rect(column.first * cacheTileSize.x, row.first * cacheTileSize.y,
					column.second * cacheTileSize.x, row.second * cacheTileSize.y),
					origin + Point(offsetX * tileSize.x, offsetY * tileSize.y),
					Point(column.second * tileSize.x, row.second * tileSize.y));
			offsetY += row.second;
		}
		offsetX += column.second;
	}
}
