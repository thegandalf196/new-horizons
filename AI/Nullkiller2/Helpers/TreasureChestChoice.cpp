/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "TreasureChestChoice.h"

namespace NK2AI
{
namespace
{
/// Gold share at which Experience plus the donation matches the Gold for a hero
/// that would otherwise take the Gold.
constexpr int EXPERIENCE_GOLD_SHARE_PERCENT = 50;
}

bool prefersChestGold(bool mainHero, bool goldPressure, int donationPercent)
{
	const bool goldFirst = !mainHero || goldPressure;
	return goldFirst && donationPercent + EXPERIENCE_GOLD_SHARE_PERCENT < 100;
}
}
