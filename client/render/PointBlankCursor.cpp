/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AssetGenerator.h"
#include "../GameEngine.h"
#include "IRenderHandler.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"

AssetGenerator::CanvasPtr AssetGenerator::createPointBlankCursor(bool penalty) const
{
	// Keep the original arrow and its hotspot at(0,0); the second unchanged
	// native arrow is only a damage cue, never another shot or projectile.
	auto image = ENGINE->renderHandler().createImage(Point(36, 36), CanvasScalingPolicy::IGNORE);
	auto canvas = image->getCanvas();
	canvas.applyTransparency(true);
	auto locator = ImageLocator(AnimationPath::builtin("CRCOMBAT"), penalty ? 15 : 3, 0, EImageBlitMode::COLORKEY);
	locator.originalDefFrame = true;
	auto arrow = ENGINE->renderHandler().loadImage(locator);
	canvas.draw(arrow, Point(0, 0));
	canvas.draw(arrow, penalty ? Point(0, 12) : Point(6, 6));
	return image;
}
