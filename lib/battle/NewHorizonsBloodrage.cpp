/*
 * NewHorizonsBloodrage.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBloodrage.h"
#include "SideInBattle.h"
#include "BattleInfo.h"
#include "../callback/IGameInfoCallback.h"

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
constexpr std::string_view AVATAR_OF_RAGE = "new-horizons:bloodrage.avatarOfRage";
constexpr std::string_view FIRST_BLOOD = "new-horizons:bloodrage.firstBlood";
constexpr std::string_view SLAYER = "new-horizons:bloodrage.slayer";
constexpr std::string_view RAGE_THROUGH_PAIN = "new-horizons:bloodrage.rageThroughPain";
}

bool validSnapshot(const SideInBattle & side)
{
	if(side.bloodrageRank < 0 || side.bloodrageRank > 3
		|| side.bloodrageDamagePercent < 0
		|| side.bloodrageSpeedBonus < 0 || side.bloodrageSpeedBonus > 1
		|| side.bloodrageAdditionalRetaliations < 0 || side.bloodrageAdditionalRetaliations > 1
		|| (side.bloodrageLowHealthIncrement != 0
			&& (side.bloodrageRank == 0 || side.bloodrageLowHealthIncrement
				!= newHorizonsBloodrage::incrementForRank(side.bloodrageRank)
					&& side.bloodrageLowHealthIncrement != 2 * newHorizonsBloodrage::incrementForRank(side.bloodrageRank)))
		|| (side.bloodragePainIncrement != 0
			&& (side.bloodrageRank == 0 || side.bloodragePainIncrement
				!= newHorizonsBloodrage::incrementForRank(side.bloodrageRank)
					&& side.bloodragePainIncrement != 2 * newHorizonsBloodrage::incrementForRank(side.bloodrageRank)))
		|| (side.bloodrageRank < 2
			&& (side.bloodrageSpeedBonus != 0 || side.bloodrageAdditionalRetaliations != 0)))
		return false;
	const auto baseCap = newHorizonsBloodrage::capForRank(side.bloodrageRank);
	const auto extendedCap = newHorizonsBloodrage::capForRank(side.bloodrageRank, true);
	return (side.bloodrageCapPercent == baseCap || side.bloodrageCapPercent == extendedCap)
		&& side.bloodrageDamagePercent <= side.bloodrageCapPercent;
}

void validateBattleStart(const BattleInfo & battle, const IGameInfoCallback & context)
{
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & side = battle.getSide(sideID);
		if(!validSnapshot(side))
			throw std::runtime_error("Invalid Bloodrage battle state");
		const auto * hero = side.heroID.hasValue() ? context.getHero(side.heroID) : nullptr;
		if(side.heroID.hasValue() && !hero)
			throw std::runtime_error("Bloodrage battle snapshot references a missing hero");
		const bool revised = revisedPerks(hero);
		if(revised && side.bloodrageRank != rank(hero))
			throw std::runtime_error("Bloodrage battle snapshot rank does not match captured hero");
		const int increment = incrementForRank(side.bloodrageRank) * (revised ? 2 : 1);
		if((side.bloodrageLowHealthIncrement != 0 && side.bloodrageLowHealthIncrement != increment)
			|| (side.bloodragePainIncrement != 0 && side.bloodragePainIncrement != increment))
			throw std::runtime_error("Bloodrage battle snapshot does not match captured perk policy");
	}
}

bool revisedPerks(const CGHeroInstance * hero)
{
	if(!hero)
		return false;
	const auto & rules = hero->getPerkState().rules;
	return rules.isStruct() && rules.Struct().contains("bloodragePolicy")
		&& rules["bloodragePolicy"]["version"].Integer() == 2;
}

int conditionalIncrement(const CGHeroInstance * hero)
{
	return incrementForRank(rank(hero)) * (revisedPerks(hero) ? 2 : 1);
}

int avatarDefenseIgnorePercent(const CGHeroInstance * hero, int attackDamagePercent,
	int capturedRank, int capturedCapPercent)
{
	if(!revisedPerks(hero))
		return atMaximumForAttack(attackDamagePercent, capturedCapPercent) ? AVATAR_DEFENSE_IGNORE_PERCENT : 0;
	const int increment = incrementForRank(capturedRank);
	if(increment <= 0 || capturedCapPercent <= 0)
		return 0;
	const int increments = std::clamp(attackDamagePercent, 0, capturedCapPercent) / increment;
	return std::min(AVATAR_DEFENSE_IGNORE_PERCENT, 5 * increments);
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

bool hasAvatarOfRage(const CGHeroInstance * hero)
{
	return hero && rank(hero) == 3
		&& hero->hasActivePerk(std::string(SKILL), std::string(AVATAR_OF_RAGE));
}

int capForHero(const CGHeroInstance * hero)
{
	return capForRank(rank(hero), hasEndlessBloodshed(hero));
}

bool hasFirstBlood(const CGHeroInstance * hero)
{
	return hero && rank(hero) > 0 && hero->hasActivePerk(std::string(SKILL), std::string(FIRST_BLOOD));
}

bool hasSlayer(const CGHeroInstance * hero)
{
	return hero && rank(hero) >= 2 && hero->hasActivePerk(std::string(SKILL), std::string(SLAYER));
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
