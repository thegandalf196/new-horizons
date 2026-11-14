/*
 * NewHorizonsMovementBonuses.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "NewHorizonsMovement.h"
#include "../bonuses/Bonus.h"

namespace newHorizonsMovement
{
struct DailyMovementLimits
{
	DailyMovementBreakdown land;
	DailyMovementBreakdown sea;
	DailyMovementBreakdown lighthouseDeparture;
};

/// Shared BonusList-compatible capacity evaluation for live turns and readback.
DLL_LINKAGE DailyMovementLimits dailyMovementLimits(const CGHeroInstance & hero,
	const TConstBonusListPtr & landBonuses, const TConstBonusListPtr & seaBonuses, int turn);
}
