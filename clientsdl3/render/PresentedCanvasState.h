/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "IScreenHandler.h"
#include <cstdint>
#include <cmath>
#include <vector>

struct SDL_Texture;

/// A single nearest-sampled cache copy preserves the old two-pass pixels only
/// when final presentation neither rescales nor shifts pixel centers.
inline bool oneToOnePresentedPixels(Point logical, bool known, float x, float y, float width, float height)
{
	return known && logical.x > 0 && logical.y > 0 && width == logical.x && height == logical.y
		&& std::isfinite(x) && std::isfinite(y) && x == std::floor(x) && y == std::floor(y);
}

/// Borrowed frame-composition input. ScreenHandler serializes access; it never
/// destroys the texture. Expected-owner clearing cannot erase a replacement.
struct PresentedCanvasState
{
	const void * owner = nullptr;
	SDL_Texture * texture = nullptr;
	uint32_t generation = 0;
	Rect clip;
	std::vector<PresentedRegion> regions;

	bool clear(const void * expectedOwner = nullptr)
	{
		if(expectedOwner && owner != expectedOwner)
			return false;
		owner = nullptr;
		texture = nullptr;
		regions.clear();
		return true;
	}

	bool current(uint32_t rendererGeneration) const
	{
		return owner && texture && generation == rendererGeneration;
	}
};
