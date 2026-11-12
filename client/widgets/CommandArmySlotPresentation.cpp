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
				const int bw = 39;
				const int bh = 14;
				const int badgeX = pos.x + pos.w - bw - 2;
				const int badgeY = pos.y + 2;
				const Rect badgeRect(badgeX, badgeY, bw, bh);
				to.drawColor(badgeRect, ColorRGBA(24, 18, 14, 235));

				const ColorRGBA goldLight(180, 135, 50, 255);
				const ColorRGBA goldDark(120, 85, 30, 255);
				to.drawLine(Point(badgeX + 1, badgeY), Point(badgeX + bw - 2, badgeY), goldLight, goldLight);
				to.drawLine(Point(badgeX, badgeY + 1), Point(badgeX, badgeY + bh - 2), goldLight, goldLight);
				to.drawLine(Point(badgeX + 1, badgeY + bh - 1), Point(badgeX + bw - 2, badgeY + bh - 1), goldDark, goldDark);
				to.drawLine(Point(badgeX + bw - 1, badgeY + 1), Point(badgeX + bw - 1, badgeY + bh - 2), goldDark, goldDark);

				const ColorRGBA goldCorner(150, 110, 40, 255);
				to.drawPoint(Point(badgeX + 1, badgeY + 1), goldCorner);
				to.drawPoint(Point(badgeX + bw - 2, badgeY + 1), goldCorner);
				to.drawPoint(Point(badgeX + 1, badgeY + bh - 2), goldCorner);
				to.drawPoint(Point(badgeX + bw - 2, badgeY + bh - 2), goldCorner);

				struct BadgePixel { int8_t x, y; uint8_t r, g, b; };
				static constexpr BadgePixel brokenSwordPixels[] = {
					{ 7, 1, 91, 49, 40 }, { 8, 1, 165, 99, 90 }, { 9, 1, 191, 132, 121 }, { 10, 1, 200, 140, 116 },
					{ 6, 2, 74, 44, 35 }, { 7, 2, 181, 111, 98 }, { 8, 2, 201, 126, 117 }, { 9, 2, 192, 118, 107 }, { 10, 2, 196, 113, 89 },
					{ 4, 3, 67, 45, 34 }, { 5, 3, 58, 40, 29 }, { 6, 3, 160, 98, 84 }, { 7, 3, 193, 111, 95 }, { 8, 3, 193, 103, 85 }, { 9, 3, 187, 92, 73 }, { 10, 3, 153, 66, 48 },
					{ 3, 4, 89, 63, 48 }, { 4, 4, 169, 136, 104 }, { 5, 4, 56, 32, 26 }, { 6, 4, 127, 87, 69 }, { 7, 4, 190, 113, 90 }, { 8, 4, 179, 88, 70 }, { 9, 4, 181, 87, 62 }, { 10, 4, 154, 61, 48 },
					{ 3, 5, 78, 50, 37 }, { 4, 5, 167, 122, 94 }, { 5, 5, 147, 111, 90 }, { 6, 5, 176, 126, 105 }, { 7, 5, 157, 79, 64 }, { 8, 5, 159, 73, 54 }, { 9, 5, 138, 58, 45 },
					{ 2, 6, 74, 41, 32 }, { 3, 6, 125, 82, 65 }, { 4, 6, 186, 151, 131 }, { 5, 6, 202, 179, 163 }, { 6, 6, 175, 135, 122 }, { 7, 6, 128, 63, 57 }, { 8, 6, 120, 59, 45 },
					{ 2, 7, 109, 58, 45 }, { 3, 7, 171, 122, 100 }, { 4, 7, 194, 169, 156 }, { 5, 7, 198, 179, 166 }, { 6, 7, 187, 152, 135 }, { 7, 7, 154, 97, 81 },
					{ 1, 8, 51, 32, 24 }, { 2, 8, 92, 38, 32 }, { 3, 8, 163, 106, 89 }, { 4, 8, 187, 152, 132 }, { 5, 8, 180, 143, 126 }, { 6, 8, 150, 101, 93 }, { 7, 8, 117, 71, 59 },
					{ 2, 9, 128, 47, 38 }, { 3, 9, 144, 67, 58 }, { 4, 9, 154, 95, 84 }, { 5, 9, 157, 100, 85 }, { 6, 9, 121, 75, 64 }, { 8, 9, 55, 23, 17 },
					{ 1, 10, 102, 39, 29 }, { 2, 10, 159, 53, 41 }, { 3, 10, 90, 36, 30 }, { 4, 10, 68, 33, 31 }, { 5, 10, 99, 58, 47 }, { 6, 10, 76, 46, 38 }, { 8, 10, 119, 37, 27 },
					{ 1, 11, 82, 34, 26 }, { 2, 11, 166, 53, 38 }, { 3, 11, 132, 47, 34 }, { 7, 11, 77, 30, 22 }, { 8, 11, 93, 32, 24 },
					{ 1, 12, 167, 61, 44 }, { 2, 12, 110, 39, 29 }, { 7, 12, 128, 43, 32 }
				};
				for(const auto & p : brokenSwordPixels)
					to.drawPoint(Point(badgeX + p.x + 1, badgeY + p.y), ColorRGBA(p.r, p.g, p.b, 255));

				to.drawText(Point(badgeX + 26, badgeY + 7), FONT_TINY, ColorRGBA(245, 240, 225, 255), ETextAlignment::CENTER, badgeText);
			}
			else
			{
				const int bw = 27;
				const int bh = 11;
				const int badgeX = pos.x + pos.w - bw - 1;
				const int badgeY = pos.y + 1;
				const Rect badgeRect(badgeX, badgeY, bw, bh);
				to.drawColor(badgeRect, ColorRGBA(24, 18, 14, 235));
				to.drawBorder(badgeRect, ColorRGBA(150, 110, 40, 255));

				to.drawPoint(Point(badgeX + 4, badgeY + 2), ColorRGBA(200, 140, 116, 255));
				to.drawPoint(Point(badgeX + 3, badgeY + 3), ColorRGBA(190, 110, 90, 255));
				to.drawPoint(Point(badgeX + 4, badgeY + 4), ColorRGBA(230, 210, 200, 255));
				to.drawPoint(Point(badgeX + 2, badgeY + 5), ColorRGBA(180, 80, 60, 255));
				to.drawPoint(Point(badgeX + 3, badgeY + 6), ColorRGBA(140, 50, 40, 255));
				to.drawPoint(Point(badgeX + 2, badgeY + 7), ColorRGBA(110, 40, 30, 255));
				to.drawPoint(Point(badgeX + 5, badgeY + 7), ColorRGBA(160, 60, 40, 255));

				to.drawText(Point(badgeX + 17, badgeY + 5), FONT_TINY, ColorRGBA(245, 240, 225, 255), ETextAlignment::CENTER, badgeText);
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
