/*
 * NewHorizonsArchery.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <string_view>

class CGHeroInstance;
namespace battle
{
class Unit;
}

namespace newHorizonsArchery
{
constexpr std::string_view SKILL = "new-horizons:archery";
constexpr std::string_view TARGET_CALLER = "new-horizons:archery.targetCaller";
constexpr std::string_view SKIRMISHER = "new-horizons:archery.skirmisher";
constexpr std::string_view POINT_BLANK_SHOT = "new-horizons:archery.pointBlankShot";
constexpr std::string_view COUNTERFIRE = "new-horizons:archery.counterfire";

constexpr int TARGET_CALLER_DAMAGE_PERCENT = 5;
constexpr int SKIRMISHER_DAMAGE_PERCENT = 75;
constexpr int COUNTERFIRE_DAMAGE_PERCENT = 50;

DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero, std::string_view perk);
DLL_LINKAGE bool hasTargetCaller(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSkirmisher(const CGHeroInstance * hero);
DLL_LINKAGE bool hasPointBlankShot(const CGHeroInstance * hero);
DLL_LINKAGE bool hasCounterfire(const CGHeroInstance * hero);
DLL_LINKAGE bool canUseSkirmisher(const CGHeroInstance * hero, const battle::Unit * shooter);
DLL_LINKAGE bool canUseCounterfire(const CGHeroInstance * hero, const battle::Unit * shooter);
}
