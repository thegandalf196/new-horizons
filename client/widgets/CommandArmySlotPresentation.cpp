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
				// Discrete, polished badge in top-right corner with gold border and dark leather background
				const int bw = 30;
				const int bh = 11;
				const int badgeX = pos.x + pos.w - bw - 2;
				const int badgeY = pos.y + 2;
				const Rect badgeRect(badgeX, badgeY, bw, bh);
				to.drawColor(badgeRect, ColorRGBA(36, 26, 18, 230));
				to.drawBorder(badgeRect, ColorRGBA(160, 120, 45, 240));

				// Miniature bright red broken sword icon
				to.drawPoint(Point(badgeX + 3, badgeY + 2), ColorRGBA(220, 130, 90, 255));
				to.drawPoint(Point(badgeX + 4, badgeY + 2), ColorRGBA(240, 150, 110, 255));
				to.drawPoint(Point(badgeX + 2, badgeY + 3), ColorRGBA(200, 70, 50, 255));
				to.drawPoint(Point(badgeX + 3, badgeY + 3), ColorRGBA(255, 200, 180, 255));
				to.drawPoint(Point(badgeX + 4, badgeY + 3), ColorRGBA(210, 80, 60, 255));
				to.drawPoint(Point(badgeX + 2, badgeY + 4), ColorRGBA(220, 70, 50, 255));
				to.drawPoint(Point(badgeX + 3, badgeY + 4), ColorRGBA(180, 50, 35, 255));
				to.drawPoint(Point(badgeX + 1, badgeY + 5), ColorRGBA(200, 160, 70, 255)); // gold hilt
				to.drawPoint(Point(badgeX + 2, badgeY + 6), ColorRGBA(220, 180, 80, 255));
				to.drawPoint(Point(badgeX + 4, badgeY + 6), ColorRGBA(170, 130, 50, 255));
				to.drawPoint(Point(badgeX + 1, badgeY + 7), ColorRGBA(150, 110, 40, 255));

				// Crisp cream text
				to.drawText(Point(badgeX + 18, badgeY + 6), FONT_TINY, ColorRGBA(245, 235, 215, 255), ETextAlignment::CENTER, badgeText);
			}
			else
			{
				// On 32x32 thumbnails: NO black background box!
				// Only a clean miniature 6x6 red broken-sword mark directly on the corner.
				const int sx = pos.x + pos.w - 8;
				const int sy = pos.y + 2;
				to.drawPoint(Point(sx + 4, sy + 0), ColorRGBA(240, 140, 100, 255));
				to.drawPoint(Point(sx + 3, sy + 1), ColorRGBA(255, 70, 50, 255));
				to.drawPoint(Point(sx + 4, sy + 1), ColorRGBA(210, 50, 35, 255));
				to.drawPoint(Point(sx + 2, sy + 2), ColorRGBA(255, 80, 60, 255));
				to.drawPoint(Point(sx + 3, sy + 2), ColorRGBA(200, 40, 30, 255));
				to.drawPoint(Point(sx + 1, sy + 3), ColorRGBA(220, 170, 75, 255)); // gold hilt
				to.drawPoint(Point(sx + 2, sy + 3), ColorRGBA(240, 190, 85, 255));
				to.drawPoint(Point(sx + 0, sy + 4), ColorRGBA(170, 120, 45, 255));
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
