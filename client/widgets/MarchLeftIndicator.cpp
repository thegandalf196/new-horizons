/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "MarchLeftIndicator.h"

#include "Images.h"

#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/pathfinder/NewHorizonsMarchLeft.h"

MarchLeftIndicator::MarchLeftIndicator(Point slotSize)
{
	OBJECT_CONSTRUCTION;
	pos.w = slotSize.x;
	pos.h = slotSize.y;
	// The hero list's movement bar, stretched to the slot's height.
	bar = std::make_shared<CAnimImage>(AnimationPath::builtin("IMOBIL"), 0, 0, 0, 0);
	bar->setScale(Point(bar->pos.w, slotSize.y));
	disable();
}

void MarchLeftIndicator::update(const CStackInstance * stack, int day)
{
	const int left = stack ? stack->getMarchLeft(day) : newHorizonsMarchLeft::FULL;
	if(left >= newHorizonsMarchLeft::FULL)
	{
		disable();
		return;
	}
	const auto frames = bar->size();
	bar->setFrame(frames > 1 ? static_cast<size_t>(static_cast<int64_t>(left) * (frames - 1) / newHorizonsMarchLeft::FULL) : 0);
	enable();
}
