/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ReachCursor.h"
#include "../GameEngine.h"
#include "IRenderHandler.h"
#include "IFont.h"
#include "EFont.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"

#include <stdexcept>

namespace reachCursor
{
namespace
{
void drawBody(Canvas & canvas, const Layout & layout)
{
	const auto & colors = palette();
	for(int y = 0; y < Body::SIZE; ++y)
		for(int x = 0; x < Body::SIZE; ++x)
		{
			const auto index = layout.body.pixels[y * Body::SIZE + x];
			if(!index)
				continue;
			const auto & color = colors[index];
			canvas.drawColor(Rect(x + layout.bodyOffset.x, y, 1, 1), ColorRGBA(color.r, color.g, color.b, color.a));
		}
}

void drawNumber(Canvas & canvas, const TextRun & row)
{
	// Native Tiny digits have no shadow pixels. Bake their one-pixel outline
	// before the foreground, so neither cursor backend draws a separate label.
	for(int y = -1; y <= 1; ++y)
		for(int x = -1; x <= 1; ++x)
			if(x || y)
				canvas.drawText(Point(row.position.x + x, row.position.y + y), FONT_TINY,
					ColorRGBA(8, 7, 5, 255), ETextAlignment::TOPLEFT, row.text);
	canvas.drawText(Point(row.position.x, row.position.y), FONT_TINY,
		ColorRGBA(254, 249, 187, 255), ETextAlignment::TOPLEFT, row.text);
}
}

Frame composeImage(const Key & key)
{
	const auto font = ENGINE->renderHandler().loadFont(FONT_TINY);
	NumberMetrics metrics;
	metrics.lineHeight = static_cast<int>(font->getLineHeight());
	for(int digit = 0; digit < 10; ++digit)
	{
		const char text[]{static_cast<char>('0' + digit), '\0'};
		metrics.digitWidths[digit] = static_cast<int>(font->getGlyphWidth(text));
	}
	const auto layout = makeLayout(key, metrics);
	if(!layout)
		throw std::runtime_error("Reach cursor requires a valid direction, maximum and native digit metrics");
	auto image = ENGINE->renderHandler().createImage(Point(layout->size.x, layout->size.y), CanvasScalingPolicy::AUTO);
	auto canvas = image->getCanvas();
	canvas.drawColor(Rect(0, 0, layout->size.x, layout->size.y), ColorRGBA(0, 0, 0, 0));
	canvas.applyTransparency(true);
	drawBody(canvas, *layout);
	for(const auto & row : layout->number)
		drawNumber(canvas, row);
	return {image, layout->hotspot};
}
}
