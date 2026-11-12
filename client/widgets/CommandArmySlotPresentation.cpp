/*
 * CommandArmySlotPresentation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CGarrisonInt.h"
#include "CommandArmySlotPresentation.h"
#include "../render/Colors.h"
#include "../render/EFont.h"
#include "render/Canvas.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>

bool CGarrisonSlot::commandLocked() const
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(getObj());
	const auto command = hero ? hero->getArmyCommand() : std::nullopt;
	return command && !newHorizonsHeroes::commandSlotView(command->effective(), ID).unlocked;
}

void CGarrisonSlot::show(Canvas & to)
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(getObj());
	const auto armyID = getObj() ? getObj()->id : ObjectInstanceID::NONE;
	const auto level = hero ? hero->level : 0;
	const int authority = hero ? hero->getPerkSkillRank("new-horizons:authority") : 0;
	if(displayedArmy != armyID || displayedLevel != level || displayedAuthorityRank != authority)
	{
		// Redraw may synchronously reenter. Command itself is still calculated
		// only by shared helpers, without reparsing the capture on every frame.
		displayedArmy = armyID;
		displayedLevel = level;
		displayedAuthorityRank = authority;
		redraw();
	}
	CIntObject::show(to);
}

void CGarrisonSlot::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	const auto * hero = dynamic_cast<const CGHeroInstance *>(getObj());
	if(hero && myStack && myStack->valid(true) && myStack->getType())
	{
		const double eff = newHorizonsHeroes::stackLeadershipEfficiency(
			hero, myStack->getType()->getId(), myStack->getCount(), hero);
		if(eff < 0.999)
		{
			const int pct = std::clamp(static_cast<int>(std::round(eff * 100.0)), 1, 99);
			const std::string badgeText = std::to_string(pct) + "%";

			const bool isSmall = owner && owner->smallIcons;
			if(!isSmall)
			{
				// Discrete, translucent pill in top-right corner
				const int bw = 27;
				const int bh = 10;
				const int badgeX = pos.x + pos.w - bw - 2;
				const int badgeY = pos.y + 2;
				const Rect badgeRect(badgeX, badgeY, bw, bh);
				to.drawColor(badgeRect, ColorRGBA(10, 8, 6, 150));
				to.drawBorder(badgeRect, ColorRGBA(90, 70, 30, 140));

				// Miniature broken sword icon
				to.drawPoint(Point(badgeX + 3, badgeY + 2), ColorRGBA(200, 130, 100, 210));
				to.drawPoint(Point(badgeX + 4, badgeY + 2), ColorRGBA(210, 140, 110, 210));
				to.drawPoint(Point(badgeX + 2, badgeY + 3), ColorRGBA(180, 100, 80, 210));
				to.drawPoint(Point(badgeX + 3, badgeY + 3), ColorRGBA(220, 190, 170, 230));
				to.drawPoint(Point(badgeX + 4, badgeY + 3), ColorRGBA(190, 100, 80, 210));
				to.drawPoint(Point(badgeX + 2, badgeY + 4), ColorRGBA(190, 90, 70, 210));
				to.drawPoint(Point(badgeX + 3, badgeY + 4), ColorRGBA(170, 80, 60, 210));
				to.drawPoint(Point(badgeX + 1, badgeY + 5), ColorRGBA(150, 60, 45, 210));
				to.drawPoint(Point(badgeX + 2, badgeY + 6), ColorRGBA(160, 50, 40, 210));
				to.drawPoint(Point(badgeX + 4, badgeY + 6), ColorRGBA(120, 40, 30, 210));
				to.drawPoint(Point(badgeX + 1, badgeY + 7), ColorRGBA(140, 45, 35, 210));

				// Discrete warm muted text
				to.drawText(Point(badgeX + 16, badgeY + 5), FONT_TINY, ColorRGBA(215, 205, 185, 190), ETextAlignment::CENTER, badgeText);
			}
			else
			{
				// Discrete 9x9 miniature corner badge for small (32x32) slots
				const int bw = 9;
				const int bh = 9;
				const int badgeX = pos.x + pos.w - bw - 1;
				const int badgeY = pos.y + 1;
				const Rect badgeRect(badgeX, badgeY, bw, bh);
				to.drawColor(badgeRect, ColorRGBA(10, 8, 6, 160));
				to.drawBorder(badgeRect, ColorRGBA(90, 70, 30, 140));

				to.drawPoint(Point(badgeX + 4, badgeY + 2), ColorRGBA(200, 130, 100, 220));
				to.drawPoint(Point(badgeX + 3, badgeY + 3), ColorRGBA(210, 150, 120, 220));
				to.drawPoint(Point(badgeX + 4, badgeY + 3), ColorRGBA(180, 90, 70, 220));
				to.drawPoint(Point(badgeX + 2, badgeY + 4), ColorRGBA(220, 190, 170, 240));
				to.drawPoint(Point(badgeX + 3, badgeY + 4), ColorRGBA(170, 80, 60, 220));
				to.drawPoint(Point(badgeX + 2, badgeY + 5), ColorRGBA(170, 70, 50, 220));
				to.drawPoint(Point(badgeX + 1, badgeY + 6), ColorRGBA(150, 50, 40, 220));
				to.drawPoint(Point(badgeX + 3, badgeY + 6), ColorRGBA(120, 40, 30, 220));
			}
		}
	}
	const auto command = hero ? hero->getArmyCommand() : std::nullopt;
	displayedArmy = getObj() ? getObj()->id : ObjectInstanceID::NONE;
	displayedLevel = hero ? hero->level : 0;
	displayedAuthorityRank = hero ? hero->getPerkSkillRank("new-horizons:authority") : 0;
	if(!command)
		return;
	const auto view = newHorizonsHeroes::commandSlotView(command->effective(), ID);
	if(owner->getSelection() == this && view.unlocked)
		to.drawBorder(Rect(pos.x + 3, pos.y + 3, pos.w - 6, pos.h - 6), Colors::YELLOW);
	if(!myStack)
	{
		const char * numeral = view.grade == newHorizonsHeroes::CommandSlotGrade::CORE ? "I"
			: view.grade == newHorizonsHeroes::CommandSlotGrade::ELITE ? "II" : "III";
		to.drawText(pos.center(), FONT_BIG, ColorRGBA(220, 220, 220, 80), ETextAlignment::CENTER, numeral);
	}
	if(view.unlocked && view.enhancementTier)
	{
		auto label = LIBRARY->generaltexth->translate(view.enhancementTier > 99
			? "new-horizons.command.enhancement.markerOverflow" : "new-horizons.command.enhancement.marker");
		boost::replace_all(label, "%TIER%", std::to_string(view.enhancementTier));
		to.drawText(Point(pos.x + pos.w - 3, pos.y + 3), FONT_TINY, Colors::WHITE, ETextAlignment::TOPRIGHT, label);
	}
}
