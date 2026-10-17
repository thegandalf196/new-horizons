/*
 * SummonHealthQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "SummonHealthQuery.h"
#include "../bonuses/IBonusBearer.h"
#include <limits>

int32_t newHorizonsElementalRebirth::querySummonMaxHP(const IBonusBearer & hypothetical)
{
	const auto value = hypothetical.valOfBonuses(BonusType::STACK_HEALTH);
	if(value > std::numeric_limits<int32_t>::max())
		return 0;
	return static_cast<int32_t>(std::max<int64_t>(1, value));
}
