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
constexpr std::string_view ARMOR_PIERCING_SHOT = "new-horizons:archery.armorPiercingShot";
constexpr std::string_view SUPPRESSION = "new-horizons:archery.suppression";
constexpr std::string_view HIGH_ARC = "new-horizons:archery.highArc";
constexpr std::string_view CROSSFIRE = "new-horizons:archery.crossfire";
constexpr std::string_view DEADEYE = "new-horizons:archery.deadeye";
constexpr std::string_view RAIN_OF_ARROWS = "new-horizons:archery.rainOfArrows";

constexpr int TARGET_CALLER_DAMAGE_PERCENT = 5;
constexpr int SKIRMISHER_DAMAGE_PERCENT = 75;
constexpr int COUNTERFIRE_DAMAGE_PERCENT = 50;
constexpr int ARMOR_PIERCING_DEFENSE_IGNORE_PERCENT = 20;
constexpr int CROSSFIRE_DAMAGE_PERCENT = 15;
constexpr int DEADEYE_DEFENSE_IGNORE_PERCENT = 25;
constexpr int RAIN_OF_ARROWS_DAMAGE_PERCENT = 35;

DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero, std::string_view perk);
DLL_LINKAGE bool hasTargetCaller(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSkirmisher(const CGHeroInstance * hero);
DLL_LINKAGE bool hasPointBlankShot(const CGHeroInstance * hero);
DLL_LINKAGE bool hasCounterfire(const CGHeroInstance * hero);
DLL_LINKAGE bool hasArmorPiercingShot(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSuppression(const CGHeroInstance * hero);
DLL_LINKAGE bool hasHighArc(const CGHeroInstance * hero);
DLL_LINKAGE bool hasCrossfire(const CGHeroInstance * hero);
DLL_LINKAGE bool hasDeadeye(const CGHeroInstance * hero);
DLL_LINKAGE bool hasRainOfArrows(const CGHeroInstance * hero);
DLL_LINKAGE bool isOrdinaryPhysicalShooter(const battle::Unit * shooter);
DLL_LINKAGE bool canUseSkirmisher(const CGHeroInstance * hero, const battle::Unit * shooter);
DLL_LINKAGE bool canUseCounterfire(const CGHeroInstance * hero, const battle::Unit * shooter);
}
