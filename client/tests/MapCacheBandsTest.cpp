/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "../mapView/CachedMapBands.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
struct Band { Rect source; Point position; Point size; };

void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

int wrap(int value, int size) { return ((value % size) + size) % size; }

void comparePixels(Rect tiles, Point tileSize, Point cacheSize, Point origin)
{
	std::vector<Band> bands;
	forEachCachedMapBand(tiles, tileSize, cacheSize, origin,
		[&](Rect source, Point position, Point size) { bands.push_back({source, position, size}); });
	require(!bands.empty() && bands.size() <= 4, "Wrapped cache must yield one to four bands");
	// Independent oracle walks individual visible pixels/tiles, not production bands.
	// Clipping a negative fractional-tile origin must leave outside pixels untouched.
	const int viewportWidth = tiles.w * tileSize.x - 3;
	const int viewportHeight = tiles.h * tileSize.y - 2;
	for(int y = 0; y < viewportHeight; ++y)
		for(int x = 0; x < viewportWidth; ++x)
		{
			int hits = 0, actualX = -1, actualY = -1;
			for(const auto & band : bands)
				if(x >= band.position.x && x < band.position.x + band.size.x
					&& y >= band.position.y && y < band.position.y + band.size.y)
				{
					++hits;
					actualX = band.source.x + (x - band.position.x) * band.source.w / band.size.x;
					actualY = band.source.y + (y - band.position.y) * band.source.h / band.size.y;
				}
			const int localX = x - origin.x, localY = y - origin.y;
			const bool inside = localX >= 0 && localY >= 0
				&& localX < tiles.w * tileSize.x && localY < tiles.h * tileSize.y;
			require(hits == (inside ? 1 : 0), "Band coverage must neither overlap nor invent outside pixels");
			if(!inside)
				continue;
			const int expectedX = wrap(tiles.x + localX / tileSize.x, tiles.w) * cacheSize.x
				+ (localX % tileSize.x) * cacheSize.x / tileSize.x;
			const int expectedY = wrap(tiles.y + localY / tileSize.y, tiles.h) * cacheSize.y
				+ (localY % tileSize.y) * cacheSize.y / tileSize.y;
			require(actualX == expectedX && actualY == expectedY,
				"Wrapped band sampling must match independent per-tile asymmetric pixel identity");
		}
}
}

int main()
{
	try
	{
		for(int firstX : {-7, -1, 0, 1, 7})
			for(int firstY : {-5, 0, 1, 5})
				for(Point zoom : {Point(16, 16), Point(24, 24), Point(32, 32), Point(48, 48)})
					comparePixels(Rect(firstX, firstY, 3, 2), zoom, Point(32, 32), Point(-7, -5));
		int emptyVisits = 0;
		forEachCachedMapBand(Rect(0, 0, 0, 2), Point(32, 32), Point(32, 32), Point(),
			[&](Rect, Point, Point) { ++emptyVisits; });
		require(emptyVisits == 0, "Empty cache must not submit any regions");
		std::cout << "Wrapped map bands match independent pixel oracle across 80 cases\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
