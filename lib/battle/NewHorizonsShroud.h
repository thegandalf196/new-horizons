/*
 * NewHorizonsShroud.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "BattleSide.h"

#include <string_view>

class CGHeroInstance;
struct Bonus;

namespace battle
{
class Unit;
}

namespace newHorizonsShroud
{
constexpr std::string_view SKILL_ID = "new-horizons:shroudOfMalassa";
constexpr std::string_view BACKSTAB_PERK_ID = "new-horizons:shroudOfMalassa.backstab";
constexpr std::string_view AMBUSHER_PERK_ID = "new-horizons:shroudOfMalassa.ambusher";
constexpr std::string_view SHADOW_ASSAULT_PERK_ID = "new-horizons:shroudOfMalassa.shadowAssault";
constexpr std::string_view NO_ESCAPE_PERK_ID = "new-horizons:shroudOfMalassa.noEscape";
constexpr std::string_view EVASIVE_SHROUD_PERK_ID = "new-horizons:shroudOfMalassa.evasiveShroud";
constexpr std::string_view AMBUSHER_STACKING_KEY = "new-horizons:shroudOfMalassa.ambusherSpent";
constexpr int SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT = 25;
constexpr std::string_view NO_ESCAPE_STACKING_KEY = "new-horizons:shroudOfMalassa.noEscape";
constexpr std::string_view EVASIVE_SHROUD_STACKING_KEY = "new-horizons:shroudOfMalassa.evasiveShroud";
constexpr int BACKSTAB_DAMAGE_PERCENT = 15;
constexpr int AMBUSHER_DAMAGE_PERCENT = 20;
constexpr int NO_ESCAPE_SPEED_PENALTY = -2;
constexpr int EVASIVE_SHROUD_REDUCTION_BASIS_POINTS = 1500;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int flankingDamagePercent(int rank);
DLL_LINKAGE int backstabDamagePercent(const CGHeroInstance * hero);
DLL_LINKAGE bool hasAmbusher(const CGHeroInstance * hero);
DLL_LINKAGE int ambusherDamagePercent(const CGHeroInstance * hero, const battle::Unit * unit);
DLL_LINKAGE Bonus ambusherSpentMarker();
DLL_LINKAGE bool isAmbusherSpentMarker(const Bonus * bonus);
DLL_LINKAGE bool hasShadowAssault(const CGHeroInstance * hero);
DLL_LINKAGE int shadowAssaultDefenseIgnorePercent(const CGHeroInstance * hero,
	const battle::Unit * target, BattleSide attackingSide);
DLL_LINKAGE Bonus shadowAssaultSpentMarker(BattleSide attackingSide);
DLL_LINKAGE bool isShadowAssaultSpentMarker(const Bonus * bonus, BattleSide attackingSide);
DLL_LINKAGE bool hasNoEscape(const CGHeroInstance * hero);
DLL_LINKAGE Bonus noEscapeSpeedPenalty();
DLL_LINKAGE bool isNoEscapeSpeedPenalty(const Bonus * bonus);
DLL_LINKAGE bool hasEvasiveShroud(const CGHeroInstance * hero);
DLL_LINKAGE Bonus evasiveShroudProtection();
DLL_LINKAGE bool isEvasiveShroudProtection(const Bonus * bonus);
DLL_LINKAGE bool deniesRetaliation(int rank);
}
