/*
 * DamageFinalization.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsArchery.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsFrozen.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
#include "../combatScripts/IDamageCalculatorScript.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsHeroRules.h"

namespace
{
constexpr int ELVEN_PRECISION_DEFENSE_IGNORE_PERCENT = 25;
constexpr int LUCKY_AIM_DEFENSE_IGNORE_PERCENT = 25;
constexpr int FORTIFICATION_ENGINEER_SIEGE_PERCENT = 125;
}

void CBattleInfoCallback::finalizeDamagePayload(const BattleAttackInfo & info,
	DamageAttackInfo & payload, const IBattleInfo * currentBattle) const
{
	const bool finalLuckPolicy = getBattle()->getLuckRollRules().finalDirectPhysicalMultiplier;
	const bool luckEligible = newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(info.attacker, info.physicalDamage);
	payload.luckyStrike = info.luckyStrike && (!finalLuckPolicy || luckEligible);
	payload.newHorizonsFinalLuck = finalLuckPolicy && luckEligible;
	if(info.physicalDamage && info.shooting && info.luckyStrike
		&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker))
	{
		if(const auto * hero = battleGetOwnerHero(info.attacker))
		{
			if(hero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"))
				payload.luckyRangedDefenseIgnorePercent += ELVEN_PRECISION_DEFENSE_IGNORE_PERCENT;
			if(hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.luckyAim"))
				payload.luckyRangedDefenseIgnorePercent += LUCKY_AIM_DEFENSE_IGNORE_PERCENT;
		}
	}
	payload.unluckyStrike = info.unluckyStrike && (!finalLuckPolicy || luckEligible);
	payload.deathBlow = info.deathBlow;
	payload.doubleDamage = info.doubleDamage;
	if(info.attacker->hasBonusOfType(BonusType::SIEGE_WEAPON))
	{
		if(const auto * hero = battleGetOwnerHero(info.attacker))
		{
			if(const auto siege = hero->getSiegeCapabilities())
			{
				const auto & capabilityRules = hero->getCapabilityRules();
				if(capabilityRules["rulesetVersion"].Integer() >= 3)
				{
					if(info.attacker->isBallista())
						payload.machineBaseDamage = siege->ballistaDamage;
					else if(info.attacker->isTurret())
					{
						int towerSiegeRating = siege->siegeRating;
						if(battleCanUseFortificationEngineer(info.attacker))
						{
							const int64_t boostedRating = static_cast<int64_t>(siege->siegeRating)
								* FORTIFICATION_ENGINEER_SIEGE_PERCENT / 100;
							const auto clampedRating = std::clamp<int64_t>(boostedRating, 0,
								static_cast<int64_t>(std::numeric_limits<int>::max()));
							towerSiegeRating = static_cast<int>(clampedRating);
						}
						payload.machineBaseDamage = newHorizonsHeroes::capabilitySiegeOutput(
							capabilityRules, towerSiegeRating, "defensiveTowerDamage");
					}
				}
				else
					payload.siegeSkillMultiplier = siege->ballistaDamageMultiplier;
			}
		}
		else if(info.attacker->isTurret())
		{
			if(const auto * town = battleGetDefendedTown())
			{
				if(const auto baseDamage = town->getNewHorizonsDefensiveTowerBaseDamage())
					payload.machineBaseDamage = *baseDamage;
			}
		}
	}
	payload.attackFactorPerPoint = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR);
	payload.attackFactorCap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR_CAP);
	payload.defenseFactorPerPoint = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR);
	payload.defenseFactorCap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR_CAP);

	if(currentBattle && newHorizonsFrozen::qualifiesForShatter(info, currentBattle->getMagicRules()))
		payload.frozenShatterFinalDamageMultiplier = 100
			+ newHorizonsFrozen::shatterBonusPercent(currentBattle->getMagicRules());
}
