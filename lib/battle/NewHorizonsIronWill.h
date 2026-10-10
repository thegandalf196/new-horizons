/*
 * NewHorizonsIronWill.h, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#pragma once

#include "HeroCommand.h"
#include "BattleSide.h"
#include "../constants/Enumerations.h"

class CGHeroInstance;
class CBattleInfoCallback;

namespace newHorizonsIronWill
{
inline constexpr const char * SKILL = "new-horizons:command";
inline constexpr const char * PERK = "new-horizons:command.ironWill";
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero);
/// Exact issuance recipients, shared by live preparation and detached forecasts.
DLL_LINKAGE std::vector<uint32_t> recipients(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order);
}
