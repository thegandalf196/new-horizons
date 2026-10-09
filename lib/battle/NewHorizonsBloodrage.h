/*
 * NewHorizonsBloodrage.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

class CGHeroInstance;

namespace newHorizonsBloodrage
{
constexpr int BASIC_INCREMENT = 5;
constexpr int ADVANCED_INCREMENT = 8;
constexpr int EXPERT_INCREMENT = 12;
constexpr int BASIC_CAP = 20;
constexpr int ADVANCED_CAP = 40;
constexpr int EXPERT_CAP = 60;
constexpr int ENDLESS_BLOODSHED_CAP_BONUS = 20;
constexpr int AVATAR_DEFENSE_IGNORE_PERCENT = 25;

constexpr bool atMaximumForAttack(int attackDamagePercent, int capturedCapPercent)
{
	return capturedCapPercent > 0 && attackDamagePercent >= capturedCapPercent;
}

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int incrementForRank(int rank);
DLL_LINKAGE int capForRank(int rank, bool endlessBloodshed = false);
DLL_LINKAGE int capForHero(const CGHeroInstance * hero);
DLL_LINKAGE bool hasWarDrums(const CGHeroInstance * hero);
DLL_LINKAGE bool hasFuryUnbound(const CGHeroInstance * hero);
DLL_LINKAGE bool hasEndlessBloodshed(const CGHeroInstance * hero);
DLL_LINKAGE bool hasUnrelenting(const CGHeroInstance * hero);
DLL_LINKAGE bool hasBerserker(const CGHeroInstance * hero);
DLL_LINKAGE bool hasBloodScent(const CGHeroInstance * hero);
DLL_LINKAGE bool hasAvatarOfRage(const CGHeroInstance * hero);
DLL_LINKAGE bool hasRageThroughPain(const CGHeroInstance * hero);
constexpr bool isValidPersonalIncrement(int value)
{
	return value == 0 || value == BASIC_INCREMENT || value == ADVANCED_INCREMENT || value == EXPERT_INCREMENT;
}
DLL_LINKAGE int initialDamagePercent(const CGHeroInstance * hero);
DLL_LINKAGE int advanceDamagePercent(const CGHeroInstance * hero, int currentPercent);
}
