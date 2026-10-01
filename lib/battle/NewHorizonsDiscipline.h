/*
 * NewHorizonsDiscipline.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"

class CGHeroInstance;

namespace newHorizonsDiscipline
{
inline constexpr const char * SKILL = "new-horizons:discipline";
inline constexpr const char * HOLD_FAST = "new-horizons:discipline.holdFast";
inline constexpr const char * FEARLESS = "new-horizons:discipline.fearless";

DLL_LINKAGE bool hasHoldFast(const CGHeroInstance * hero);
DLL_LINKAGE bool hasFearless(const CGHeroInstance * hero);
DLL_LINKAGE Bonus holdFastMoraleFloorBonus();
DLL_LINKAGE bool isHoldFastMoraleFloorBonus(const Bonus * bonus);
DLL_LINKAGE CSelector holdFastMoraleFloorBonusSelector();
}
