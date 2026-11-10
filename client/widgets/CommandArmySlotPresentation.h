/*
 * CommandArmySlotPresentation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/entities/hero/NewHorizonsCommandRules.h"
#include "../../lib/Color.h"

namespace commandArmySlotPresentation
{
/// Permission color is independent of the occupant and enhancement tier.
inline ColorRGBA border(newHorizonsHeroes::CommandSlotGrade grade)
{
	using newHorizonsHeroes::CommandSlotGrade;
	switch(grade)
	{
	case CommandSlotGrade::CORE: return ColorRGBA(173, 113, 61);
	case CommandSlotGrade::ELITE: return ColorRGBA(192, 198, 205);
	case CommandSlotGrade::CHAMPION: return ColorRGBA(222, 181, 62);
	}
	return ColorRGBA(128, 128, 128);
}
}
