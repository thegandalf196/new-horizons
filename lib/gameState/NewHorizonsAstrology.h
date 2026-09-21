/*
 * NewHorizonsAstrology.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 *
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../constants/Enumerations.h"

/// The server-authored result of one creature-growth ("Astrology") week.
///
/// FIRST_WEEK is deliberately used as the not-yet-authored sentinel.  All
/// actual week types, including NORMAL, are valid results and must not be
/// inferred from the presence of a creature ID.
struct DLL_LINKAGE AstrologyWeek
{
	EWeekType type = EWeekType::FIRST_WEEK;
	CreatureID creature = CreatureID::NONE;
	int additionalGrowth = 0;

	bool known() const
	{
		return type != EWeekType::FIRST_WEEK;
	}

	bool operator==(const AstrologyWeek &) const = default;

	template <typename Handler>
	void serialize(Handler & h)
	{
		h & type;
		h & creature;
		h & additionalGrowth;
	}
};
