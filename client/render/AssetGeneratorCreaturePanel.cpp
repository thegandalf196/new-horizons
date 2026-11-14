/*
 * Creature-panel resources, part of VCMI / New Horizons.
 * Authors: listed in AUTHORS; GNU General Public License v2.0 or later.
 * See license.txt for the complete license.
 */
#include "StdInc.h"
#include "AssetGenerator.h"
#include "../GameEngine.h"
#include "../windows/CreatureStatLayout.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "Colors.h"
#include "IRenderHandler.h"
#include <array>
#include <map>

void AssetGenerator::registerCreaturePanelAssets()
{
	imageFiles[ImagePath::builtin("stackWindow/info-panel-0.png")] = [this](){ return createCreatureInfoPanel(2);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-1.png")] = [this](){ return createCreatureInfoPanel(3);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-2.png")] = [this](){ return createCreatureInfoPanel(4);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-0.png")] = [this](){ return createCreatureInfoPanel(2, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-1.png")] = [this](){ return createCreatureInfoPanel(3, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-2.png")] = [this](){ return createCreatureInfoPanel(4, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-rank-0.png")] = [this](){ return createCreatureInfoPanel(2, false, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-rank-1.png")] = [this](){ return createCreatureInfoPanel(3, false, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-rank-2.png")] = [this](){ return createCreatureInfoPanel(4, false, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-rank-0.png")] = [this](){ return createCreatureInfoPanel(2, true, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-rank-1.png")] = [this](){ return createCreatureInfoPanel(3, true, true);};
	imageFiles[ImagePath::builtin("stackWindow/info-panel-nh-rank-2.png")] = [this](){ return createCreatureInfoPanel(4, true, true);};
	imageFiles[ImagePath::builtin("stackWindow/iconInitiative.png")] = [this](){ return createCreatureInitiativeIcon(); };
	imageFiles[ImagePath::builtin("stackWindow/iconRetaliations.png")] = [this](){ return createCreatureStatIcon(CreatureStatGlyph::RETALIATIONS); };
	imageFiles[ImagePath::builtin("stackWindow/iconPhysicalResistance.png")] = [this](){ return createCreatureStatIcon(CreatureStatGlyph::PHYSICAL_RESISTANCE); };
	imageFiles[ImagePath::builtin("stackWindow/iconMagicalResistance.png")] = [this](){ return createCreatureStatIcon(CreatureStatGlyph::MAGICAL_RESISTANCE); };
	imageFiles[ImagePath::builtin("stackWindow/bonus-effects.png")] = [this](){ return createCreatureInfoPanelElement(BONUS_EFFECTS);};
	imageFiles[ImagePath::builtin("stackWindow/spell-effects.png")] = [this](){ return createCreatureInfoPanelElement(SPELL_EFFECTS);};
	imageFiles[ImagePath::builtin("stackWindow/leadership.png")] = [this](){ return createCreatureInfoPanelElement(LEADERSHIP);};
	imageFiles[ImagePath::builtin("stackWindow/button-panel.png")] = [this](){ return createCreatureInfoPanelElement(BUTTON_PANEL);};
	imageFiles[ImagePath::builtin("stackWindow/commander-bg.png")] = [this](){ return createCreatureInfoPanelElement(COMMANDER_BACKGROUND);};
	imageFiles[ImagePath::builtin("stackWindow/commander-abilities.png")] = [this](){ return createCreatureInfoPanelElement(COMMANDER_ABILITIES);};
	imageFiles[ImagePath::builtin("stackWindow/dead-commander-overlay.png")] = [this](){ return createDeadCommanderOverlay();};
}

namespace
{
void drawRetaliationGlyph(Canvas & canvas)
{
	const ColorRGBA gold(210, 186, 116, ColorRGBA::ALPHA_OPAQUE);
	const ColorRGBA highlight(255, 240, 168, ColorRGBA::ALPHA_OPAQUE);
	const std::array<Point, 6> upper{Point(3, 6), Point(4, 4), Point(6, 2), Point(10, 2), Point(12, 4), Point(14, 7)};
	const std::array<Point, 6> lower{Point(13, 12), Point(12, 14), Point(10, 16), Point(6, 16), Point(4, 14), Point(2, 11)};
	for(const auto & arc : {upper, lower})
		for(size_t i = 1; i < arc.size(); ++i)
			canvas.drawLine(arc[i - 1], arc[i], gold, gold);
	canvas.drawLine(Point(14, 7), Point(10, 6), highlight, gold);
	canvas.drawLine(Point(14, 7), Point(14, 3), highlight, gold);
	canvas.drawLine(Point(2, 11), Point(6, 12), highlight, gold);
	canvas.drawLine(Point(2, 11), Point(2, 15), highlight, gold);
}

void drawResistanceGlyph(Canvas & canvas, bool magical)
{
	const ColorRGBA gold(210, 186, 116, ColorRGBA::ALPHA_OPAQUE);
	const ColorRGBA highlight(255, 240, 168, ColorRGBA::ALPHA_OPAQUE);
	const std::array<Point, 8> outline{Point(3, 3), Point(13, 3), Point(13, 10), Point(11, 14),
		Point(8, 16), Point(5, 14), Point(3, 10), Point(3, 3)};
	for(size_t i = 1; i < outline.size(); ++i)
		canvas.drawLine(outline[i - 1], outline[i], gold, gold);
	if(magical)
	{
		canvas.drawLine(Point(8, 6), Point(8, 12), highlight, highlight);
		canvas.drawLine(Point(5, 9), Point(11, 9), highlight, highlight);
		canvas.drawLine(Point(6, 7), Point(10, 11), gold, gold);
		canvas.drawLine(Point(10, 7), Point(6, 11), gold, gold);
	}
	else
	{
		const std::array<Point, 8> inner{Point(5, 5), Point(11, 5), Point(11, 10), Point(10, 12),
			Point(8, 14), Point(6, 12), Point(5, 10), Point(5, 5)};
		for(size_t i = 1; i < inner.size(); ++i)
			canvas.drawLine(inner[i - 1], inner[i], highlight, gold);
	}
}
}

AssetGenerator::CanvasPtr AssetGenerator::createCreatureStatIcon(CreatureStatGlyph glyph) const
{
	const Point size(17, 19);
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	canvas.drawColor(Rect(Point(0, 0), size), Colors::TRANSPARENCY);
	if(glyph == CreatureStatGlyph::RETALIATIONS)
		drawRetaliationGlyph(canvas);
	else
		drawResistanceGlyph(canvas, glyph == CreatureStatGlyph::MAGICAL_RESISTANCE);
	return image;
}

AssetGenerator::CanvasPtr AssetGenerator::createCreatureInfoPanel(int boxesAmount, bool showNewHorizonsStats, bool showRank) const
{
	// The shared row layout preserves the legacy footprint and adds the four
	// NH-only statistics without stretching the native panel or its portrait.
	const int statRows = creatureStatLayout::panelRows(showNewHorizonsStats, showRank);
	Point size(438, creatureStatLayout::panelHeight(showNewHorizonsStats, showRank));

	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();

	Rect r(4, 40, 102, 132);
	canvas.drawColor(r, Colors::BLACK);
	canvas.drawBorder(r, Colors::YELLOW);

    const ColorRGBA rectangleColor = ColorRGBA(0, 0, 0, 75);
    const ColorRGBA rectangleColorRed = ColorRGBA(32, 0, 0, 150);
    const ColorRGBA borderColor = ColorRGBA(128, 100, 75);

	r = Rect(60, 3, 315, 21);
	canvas.drawColorBlended(r, rectangleColor);
	canvas.drawBorder(r, borderColor);

	for(int i = 0; i < statRows; i++)
	{
		Rect r(114, 30 + i * 19, 24, 20);
		canvas.drawColorBlended(r, rectangleColor);
		canvas.drawBorder(r, borderColor);
		r.x += 23;
		r.w = 173;
		canvas.drawColorBlended(r, rectangleColor);
		canvas.drawBorder(r, borderColor);
	}

	std::vector<Rect> redRects = {
		Rect(319, 30, 45, 45),
		Rect(373, 30, 45, 45)
	};
	std::vector<Rect> darkRects = {};

	if(boxesAmount == 3)
	{
		redRects.push_back(Rect(347, 109, 45, 45));
		darkRects.push_back(Rect(347, 156, 45, 19));
	}
	else if(boxesAmount == 4)
	{
		redRects.push_back(Rect(319, 109, 45, 45));
		redRects.push_back(Rect(373, 109, 45, 45));
		darkRects.push_back(Rect(319, 156, 45, 19));
		darkRects.push_back(Rect(373, 156, 45, 19));
	}

	for(auto & rect : darkRects)
	{
		canvas.drawColorBlended(rect, rectangleColor);
		canvas.drawBorder(rect, borderColor);
	}
	for(auto & rect : redRects)
	{
		canvas.drawColorBlended(rect, rectangleColorRed);
		canvas.drawBorder(rect, borderColor);
	}

	return image;
}

AssetGenerator::CanvasPtr AssetGenerator::createCreatureInitiativeIcon() const
{
	// Small, deliberately monochromatic placeholder art for the new stat.  A
	// compact hourglass reads as turn timing at the same scale as the existing
	// creature-stat glyphs and keeps this UI self-contained until definitive
	// artwork is commissioned.
	const Point size(17, 19);
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	canvas.drawColor(Rect(Point(0, 0), size), Colors::TRANSPARENCY);

	const ColorRGBA frame(210, 186, 116, ColorRGBA::ALPHA_OPAQUE);
	const ColorRGBA highlight(255, 240, 168, ColorRGBA::ALPHA_OPAQUE);
	const ColorRGBA sand(176, 130, 56, ColorRGBA::ALPHA_OPAQUE);

	canvas.drawLine(Point(2, 1), Point(14, 1), frame, frame);
	canvas.drawLine(Point(2, 17), Point(14, 17), frame, frame);
	canvas.drawLine(Point(3, 2), Point(13, 2), highlight, highlight);
	canvas.drawLine(Point(3, 16), Point(13, 16), highlight, highlight);
	canvas.drawLine(Point(3, 3), Point(8, 9), frame, frame);
	canvas.drawLine(Point(13, 3), Point(8, 9), frame, frame);
	canvas.drawLine(Point(3, 15), Point(8, 9), frame, frame);
	canvas.drawLine(Point(13, 15), Point(8, 9), frame, frame);
	canvas.drawColor(Rect(6, 7, 5, 3), sand);
	canvas.drawColor(Rect(7, 11, 3, 3), sand);
	canvas.drawPoint(Point(5, 5), highlight);
	canvas.drawPoint(Point(11, 13), highlight);

	return image;
}

AssetGenerator::CanvasPtr AssetGenerator::createCreatureInfoPanelElement(CreatureInfoPanelElement element) const
{
	std::map<CreatureInfoPanelElement, Point> size {
		{BONUS_EFFECTS, Point(438, 59)},
		{SPELL_EFFECTS, Point(438, 42)},
		{LEADERSHIP, Point(438, 59)},
		{BUTTON_PANEL, Point(438, creatureStatLayout::BUTTON_PANEL_HEIGHT)},
		{COMMANDER_BACKGROUND, Point(438, 177)},
		{COMMANDER_ABILITIES, Point(438, 59)}
	};

	auto image = ENGINE->renderHandler().createImage(size[element], CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();

    const ColorRGBA rectangleColor = ColorRGBA(0, 0, 0, 75);
    const ColorRGBA rectangleColorRed = ColorRGBA(32, 0, 0, 150);
    const ColorRGBA borderColor = ColorRGBA(128, 100, 75);

	switch (element)
	{
	case BONUS_EFFECTS:
		for(int i = 0; i < 2; i++)
		{
			Rect r(4 + i * 208, 0, 54, 54);
			canvas.drawColorBlended(r, rectangleColorRed);
			canvas.drawBorder(r, borderColor);
			r = Rect(61 + i * 208, 0, 144, 54);
			canvas.drawColorBlended(r, rectangleColor);
			canvas.drawBorder(r, borderColor);
		}
		break;
	case SPELL_EFFECTS:
		for(int i = 0; i < 8; i++)
		{
			Rect r(6 + i * 54, 2, 48, 36);
			canvas.drawColorBlended(r, rectangleColor);
			canvas.drawBorder(r, borderColor);
		}
		break;
	case LEADERSHIP:
	{
		Rect iconFrame(8, 10, 36, 36);
		canvas.drawColorBlended(iconFrame, rectangleColorRed);
		canvas.drawBorder(iconFrame, borderColor);
		Rect textFrame(48, 3, 382, 53);
		canvas.drawColorBlended(textFrame, rectangleColor);
		canvas.drawBorder(textFrame, borderColor);
		break;
	}
	case BUTTON_PANEL:
		canvas.drawColorBlended(Rect(382, 5, 52, 36), Colors::BLACK);
		break;
	case COMMANDER_BACKGROUND:
		for(int x = 0; x < 3; x++)
		{
			for(int y = 0; y < 3; y++)
			{
				Rect r(269 + x * 52, 21 + y * 52, 44, 44);
				canvas.drawColorBlended(r, rectangleColorRed);
				canvas.drawBorder(r, borderColor);
			}
		}
		for(int x = 0; x < 3; x++)
		{
			for(int y = 0; y < 2; y++)
			{
				Rect r(10 + x * 80, 20 + y * 80, 70, 70);
				canvas.drawColor(r, Colors::BLACK);
			}
		}
		break;
	case COMMANDER_ABILITIES:
		for(int i = 0; i < 6; i++)
		{
			Rect r(37 + i * 63, 2, 54, 54);
			canvas.drawColorBlended(r, rectangleColorRed);
			canvas.drawBorder(r, borderColor);
		}
		for(int i = 0; i < 2; i++)
		{
			Rect r(10 + i * 401, 6, 22, 46);
			canvas.drawColor(r, Colors::BLACK);
		}
		break;
	}

	return image;
}
