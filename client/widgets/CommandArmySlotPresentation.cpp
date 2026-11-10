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
	const auto command = hero ? hero->getArmyCommand() : std::nullopt;
	displayedArmy = getObj() ? getObj()->id : ObjectInstanceID::NONE;
	displayedLevel = hero ? hero->level : 0;
	displayedAuthorityRank = hero ? hero->getPerkSkillRank("new-horizons:authority") : 0;
	if(!command)
		return;
	const auto view = newHorizonsHeroes::commandSlotView(command->effective(), ID);
	const auto color = commandArmySlotPresentation::border(view.grade);
	to.drawBorder(pos, color, 2);
	if(owner->getSelection() == this && view.unlocked)
		to.drawBorder(Rect(pos.x + 3, pos.y + 3, pos.w - 6, pos.h - 6), Colors::YELLOW);
	if(!view.unlocked)
	{
		to.drawColorBlended(Rect(pos.x + 2, pos.y + 2, pos.w - 4, pos.h - 4), ColorRGBA(90, 90, 90, 165));
		to.drawText(pos.center(), FONT_TINY, Colors::WHITE, ETextAlignment::CENTER,
			LIBRARY->generaltexth->translate("new-horizons.command.slot.lockedMarker"));
	}
	else if(view.enhancementTier)
	{
		auto label = LIBRARY->generaltexth->translate(view.enhancementTier > 99
			? "new-horizons.command.enhancement.markerOverflow" : "new-horizons.command.enhancement.marker");
		boost::replace_all(label, "%TIER%", std::to_string(view.enhancementTier));
		to.drawText(Point(pos.x + pos.w - 3, pos.y + 3), FONT_TINY, color, ETextAlignment::TOPRIGHT, label);
	}
}
