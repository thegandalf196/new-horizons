/*
 * CommandArmySlotPresentation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGarrisonInt.h"
#include "LeadershipEfficiencyPresentation.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

bool CGarrisonSlot::commandLocked() const
{
	// All seven ordinary slots are available, independently of creature tier.
	return false;
}

int CGarrisonSlot::leadershipPercent()
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(getObj());
	if(!hero || !myStack || !myStack->valid(true) || !myStack->getType())
		return -1;
	const auto creature = myStack->getType()->getId();
	const auto count = myStack->getCount();
	const auto & readout = leadershipReadout.get(hero, creature, count, hero, hero, [&]()
	{
		return newHorizonsHeroes::leadershipStackReadout(hero, creature, count, nullptr, hero);
	});
	return readout.active && leadershipPresentation::reducedEfficiency(readout.efficiency)
		? leadershipPresentation::compactPercent(readout.efficiency) : -1;
}

void CGarrisonSlot::drawLeadershipBadge(Canvas & canvas)
{
	const int percent = leadershipPercent();
	if(percent < 0)
		return;
	const auto role = owner->smallIcons ? leadershipPresentation::BadgeRole::MINIATURE
		: leadershipPresentation::BadgeRole::PORTRAIT;
	const auto size = leadershipPresentation::badgeSize(role);
	leadershipPresentation::drawBadge(canvas, Point(pos.x + pos.w - size.width - 1, pos.y + 1), percent, role);
}

void CGarrisonSlot::show(Canvas & canvas)
{
	const int percent = leadershipPercent();
	if(displayedLeadershipPercent != percent)
	{
		// Publish before redraw, which can synchronously reenter this widget.
		displayedLeadershipPercent = percent;
		redraw();
	}
	CIntObject::show(canvas);
	drawLeadershipBadge(canvas);
}

void CGarrisonSlot::showAll(Canvas & canvas)
{
	displayedLeadershipPercent = leadershipPercent();
	CIntObject::showAll(canvas);
	drawLeadershipBadge(canvas);
}
