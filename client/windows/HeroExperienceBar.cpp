/*
 * HeroExperienceBar.cpp, part of VCMI engine / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroExperienceBar.h"
#include "render/Canvas.h"

HeroExperienceBar::HeroExperienceBar(Point pos_, Point size_)
	: CIntObject(0)
{
	pos.x = pos_.x;
	pos.y = pos_.y;
	pos.w = size_.x;
	pos.h = size_.y;
}

void HeroExperienceBar::setProgress(double ratio)
{
	progress = std::clamp(ratio, 0.0, 1.0);
}

void HeroExperienceBar::show(Canvas & to)
{
	const Rect r(pos.x, pos.y, pos.w, pos.h);
	if(r.w <= 0 || r.h <= 0)
		return;

	// Dark trough background
	to.drawColor(r, ColorRGBA(24, 18, 14, 255));

	// Outer beveled frame: shade on top/left, light on bottom/right
	const ColorRGBA shade(45, 31, 20, 255);
	const ColorRGBA light(128, 100, 75, 255);
	to.drawLine(Point(r.x, r.y), Point(r.x + r.w - 1, r.y), shade, shade);
	to.drawLine(Point(r.x, r.y), Point(r.x, r.y + r.h - 1), shade, shade);
	to.drawLine(Point(r.x, r.y + r.h - 1), Point(r.x + r.w - 1, r.y + r.h - 1), light, light);
	to.drawLine(Point(r.x + r.w - 1, r.y), Point(r.x + r.w - 1, r.y + r.h - 1), light, light);

	// Inner bevel edge
	if(r.w > 4 && r.h > 4)
		to.drawBorder(Rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2), ColorRGBA(15, 10, 8, 255));

	// Glowing blue progress bar
	const int innerW = r.w - 4;
	const int innerH = r.h - 4;
	if(innerW > 0 && innerH > 0)
	{
		const int fillW = static_cast<int>(progress * innerW);
		if(fillW > 0)
		{
			const Rect fillRect(r.x + 2, r.y + 2, fillW, innerH);
			to.drawColor(fillRect, ColorRGBA(28, 108, 228, 255));
			if(innerH >= 4)
			{
				to.drawColor(Rect(fillRect.x, fillRect.y, fillRect.w, std::max(1, innerH / 3)), ColorRGBA(96, 178, 255, 255));
				to.drawColor(Rect(fillRect.x, fillRect.y + innerH - 2, fillRect.w, 2), ColorRGBA(16, 72, 170, 255));
			}
		}
	}
}

void HeroExperienceBar::showAll(Canvas & to)
{
	show(to);
}
