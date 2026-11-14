/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "FortRecruitmentPresentation.h"
#include "../GameEngine.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Images.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "render/Colors.h"
#include "render/EFont.h"
#include "render/IRenderHandler.h"
#include "render/IFont.h"

namespace fortRecruitmentPresentation
{
int rankedFortTinyFontHeight()
{
	return static_cast<int>(ENGINE->renderHandler().loadFont(FONT_TINY)->getLineHeight());
}

int rankedFortStatTop(bool compactStatGrid, int compactPortraitHeight)
{
	const int titleBottom = 1 + rankedFortTinyFontHeight() + 1;
	return titleBottom + (compactStatGrid ? compactPortraitHeight : 0);
}

int rankedFortStatBottomPadding()
{
	return std::max(2, rankedFortTinyFontHeight() / 2);
}

int rankedFortStatMinimumRowHeight()
{
	return rankedFortTinyFontHeight() + 1;
}

int rankedFortStatRowCount(bool compactStatGrid)
{
	return compactStatGrid ? NH_FORT_COMPACT_STAT_ROWS : NH_FORT_STAT_COUNT;
}

int rankedFortMinimumCardHeight(bool compactStatGrid, int compactPortraitHeight)
{
	return rankedFortStatTop(compactStatGrid, compactPortraitHeight)
		+ rankedFortStatRowCount(compactStatGrid) * rankedFortStatMinimumRowHeight()
		+ rankedFortStatBottomPadding();
}

int rankedFortStatRowHeight(int cardHeight, bool compactStatGrid,
	int compactPortraitHeight)
{
	return std::max(rankedFortStatMinimumRowHeight(),
		(cardHeight - rankedFortStatTop(compactStatGrid, compactPortraitHeight) - rankedFortStatBottomPadding())
			/ rankedFortStatRowCount(compactStatGrid));
}

int rankedFortConfluxCompactPortraitHeight(int cardHeight)
{
	const int minimumStatAreaHeight = NH_FORT_COMPACT_STAT_ROWS * rankedFortStatMinimumRowHeight();
	const int availablePortraitHeight = cardHeight - rankedFortStatTop() - minimumStatAreaHeight
		- rankedFortStatBottomPadding();
	return std::clamp(availablePortraitHeight, NH_FORT_COMPACT_PORTRAIT_HEIGHT,
		NH_FORT_CONFLUX_COMPACT_PORTRAIT_MAX_HEIGHT);
}

std::shared_ptr<CPicture> createResponsiveFortCardBackground(const Point & size)
{
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::AUTO);
	Canvas canvas = image->getCanvas();
	canvas.fillTexture(ENGINE->renderHandler().loadImage(
		ImageLocator(ImagePath::builtin("DiBoxBck"), EImageBlitMode::OPAQUE)));
	canvas.drawColorBlended(Rect(1, 1, size.x - 2, size.y - 2), ColorRGBA(0, 0, 0, 92));
	canvas.drawBorder(Rect(0, 0, size.x, size.y), ColorRGBA(165, 122, 55));
	canvas.drawBorder(Rect(2, 2, size.x - 4, size.y - 4), ColorRGBA(70, 46, 28));
	return std::make_shared<CPicture>(std::static_pointer_cast<IImage>(image), Point(0, 0));
}

namespace
{
class RankedFortCreatureViewport : public CIntObject
{
public:
	RankedFortCreatureViewport(const Rect & viewport, const CCreature * creature, bool contain)
		: CIntObject(0, viewport.topLeft())
	{
		pos.w = viewport.w;
		pos.h = viewport.h;
		OBJECT_CONSTRUCTION;
		new CCreaturePic(0, 0, creature, false, true, contain ? viewport.dimensions() : Point());
	}

	void show(Canvas & to) override
	{
		CanvasClipRectGuard clip(to, pos);
		CIntObject::show(to);
	}

	void showAll(Canvas & to) override
	{
		CanvasClipRectGuard clip(to, pos);
		CIntObject::showAll(to);
	}
};
}

void createCreaturePreview(const Rect & viewport, const CCreature * creature, bool contain)
{
	new RankedFortCreatureViewport(viewport, creature, contain);
}
}
