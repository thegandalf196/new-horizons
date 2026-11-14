/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/Rect.h"

#include <algorithm>
#include <cmath>

namespace creaturePreviewFit
{
// Image content bounds are in native logical coordinates, irrespective of the
// screen's scaling factor. Retain one union/transform for the entire preview.
inline Rect include(const Rect & accumulated, const Rect & frame)
{
	if(frame.w <= 0 || frame.h <= 0)
		return accumulated;
	if(accumulated.w <= 0 || accumulated.h <= 0)
		return frame;
	const int left = std::min(accumulated.x, frame.x);
	const int top = std::min(accumulated.y, frame.y);
	return Rect(left, top, std::max(accumulated.right(), frame.right()) - left,
		std::max(accumulated.bottom(), frame.bottom()) - top);
}

inline Rect destination(const Rect & bounds, const Point & viewport)
{
	if(bounds.w <= 0 || bounds.h <= 0 || viewport.x <= 0 || viewport.y <= 0)
		return Rect(0, 0, 0, 0);
	const double scale = std::min(1.0, std::min(static_cast<double>(viewport.x) / bounds.w,
		static_cast<double>(viewport.y) / bounds.h));
	const int width = std::max(1, static_cast<int>(std::floor(bounds.w * scale)));
	const int height = std::max(1, static_cast<int>(std::floor(bounds.h * scale)));
	return Rect((viewport.x - width) / 2, (viewport.y - height) / 2, width, height);
}
}
