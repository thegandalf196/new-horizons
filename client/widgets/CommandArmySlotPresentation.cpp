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
			const int bw = 24;
			const int bh = 10;
			const Rect badgeRect(pos.x + pos.w - bw - 2, pos.y + 2, bw, bh);
			to.drawColor(badgeRect, ColorRGBA(35, 10, 10, 220));
			to.drawBorder(badgeRect, ColorRGBA(220, 60, 60, 240));
			to.drawText(Point(badgeRect.x + bw / 2, badgeRect.y + 1), FONT_TINY, ColorRGBA(255, 160, 160, 255), ETextAlignment::TOPCENTER, badgeText);
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
