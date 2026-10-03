/*
 * NewHorizonsBloodrage.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBloodrage.h"

#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsBloodrage
{
namespace
{
constexpr std::string_view SKILL = "new-horizons:bloodrage";
constexpr std::string_view WAR_DRUMS = "new-horizons:bloodrage.warDrums";
constexpr std::string_view FURY_UNBOUND = "new-horizons:bloodrage.furyUnbound";
constexpr std::string_view ENDLESS_BLOODSHED = "new-horizons:bloodrage.endlessBloodshed";
constexpr std::string_view UNRELENTING = "new-horizons:bloodrage.unrelenting";
constexpr std::string_view BERSERKER = "new-horizons:bloodrage.berserker";
constexpr std::string_view BLOOD_SCENT = "new-horizons:bloodrage.bloodScent";
constexpr std::string_view RAGE_THROUGH_PAIN = "new-horizons:bloodrage.rageThroughPain";
}

int rank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank(std::string(SKILL)), 0, 3);
	}
	catch(const std::exception &)
	{
		// Core-only and legacy profiles do not register the fork-specific skill.
		return 0;
	}
}

int incrementForRank(int value)
{
	switch(value)
	{
		case 1: return BASIC_INCREMENT;
		case 2: return ADVANCED_INCREMENT;
		case 3: return EXPERT_INCREMENT;
		default: return 0;
	}
}

int capForRank(int value, bool endlessBloodshed)
{
	switch(value)
	{
		case 1: return BASIC_CAP;
		case 2: return ADVANCED_CAP;
		case 3: return EXPERT_CAP + (endlessBloodshed ? ENDLESS_BLOODSHED_CAP_BONUS : 0);
		default: return 0;
	}
}

bool hasFuryUnbound(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(FURY_UNBOUND));
}

bool hasEndlessBloodshed(const CGHeroInstance * hero)
{
	return rank(hero) == 3 && hero && hero->hasActivePerk(std::string(SKILL), std::string(ENDLESS_BLOODSHED));
}

bool hasUnrelenting(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(UNRELENTING));
}

bool hasBerserker(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(BERSERKER));
}

bool hasBloodScent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(BLOOD_SCENT));
}

bool hasRageThroughPain(const CGHeroInstance * hero)
{
	return hero && rank(hero) > 0
		&& hero->hasActivePerk(std::string(SKILL), std::string(RAGE_THROUGH_PAIN));
}

int capForHero(const CGHeroInstance * hero)
{
	return capForRank(rank(hero), hasEndlessBloodshed(hero));
}

bool hasWarDrums(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(WAR_DRUMS));
}

int initialDamagePercent(const CGHeroInstance * hero)
{
	const int heroRank = rank(hero);
	return heroRank > 0 && hasWarDrums(hero) ? incrementForRank(heroRank) : 0;
}

int advanceDamagePercent(const CGHeroInstance * hero, int currentPercent)
{
	const int heroRank = rank(hero);
	return std::min(capForHero(hero), std::max(0, currentPercent) + incrementForRank(heroRank));
}
}
