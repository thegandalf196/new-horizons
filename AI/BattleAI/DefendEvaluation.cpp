/* New Horizons Defend evaluation; GPL v2.0 or later, see license.txt. */
#include "StdInc.h"
#include "DefendEvaluation.h"
#include "BattleDamageValue.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/bonuses/BonusList.h"
#include <algorithm>
#include <functional>
#include <vector>
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
bool hasDefendRetaliationMarker(const battle::Unit & stack)
{
	const auto bonuses = stack.getBonusesOfType(BonusType::ADDITIONAL_RETALIATION);
	return bonuses && std::any_of(bonuses->begin(), bonuses->end(), [](const auto & bonus)
	{
		return newHorizonsMaintenanceSpecialties::isDefendRetaliationBonus(bonus.get()) && bonus->val > 0;
	});
}
}

namespace defendEvaluation
{
Capabilities capabilities(const HypotheticBattle & battle, const battle::Unit & stack)
{
	Capabilities result;
	result.hero = battle.battleGetOwnerHero(&stack);
	result.bulwarkRank = newHorizonsBulwark::rank(result.hero);
	const int paviseReduction = newHorizonsCombatSkills::paviseReductionPercent(result.hero);
	const bool battlecraft = newHorizonsBattlecraft::hasPreemptiveStrike(result.hero);
	result.holdFast = newHorizonsDiscipline::hasHoldFast(result.hero);
	const bool bastion = result.hero && result.hero->hasActivePerk(
		std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID),
		std::string(newHorizonsCombatSkills::BASTION_PERK_ID));
	result.battlefieldMastery = newHorizonsBattlecraft::hasBattlefieldMastery(result.hero);
	result.hasDefendValue = result.bulwarkRank > 0 || paviseReduction > 0
		|| battlecraft || result.holdFast || bastion;
	result.maintenance = newHorizonsMaintenanceSpecialties::woundHealing(battle, stack,
		newHorizonsMaintenanceSpecialties::Trigger::VOLUNTARY_DEFEND) > 0
		|| hasDefendRetaliationMarker(stack);
	result.hasDefendValue = result.hasDefendValue || result.maintenance;
	return result;
}

int projectWoundHealing(HypotheticBattle & battle, StackWithBonuses & target)
{
	int64_t amount = newHorizonsMaintenanceSpecialties::woundHealing(battle, target,
		newHorizonsMaintenanceSpecialties::Trigger::VOLUNTARY_DEFEND);
	if(amount <= 0)
		return 0;
	const auto before = target.getAvailableHealth();
	target.heal(amount, EHealLevel::HEAL, EHealPower::PERMANENT);
	return static_cast<int>(target.getAvailableHealth() - before);
}

float extraRetaliationValue(const HypotheticBattle & before,
	const std::shared_ptr<HypotheticBattle> & defended, const battle::Unit & original)
{
	const auto * projected = defended->battleGetUnitByID(original.unitId());
	if(!projected || !projected->alive()
		|| newHorizonsMaintenanceSpecialties::defendRetaliations(*projected) <= 0
		|| projected->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS)
		|| !projected->ableToRetaliate())
		return 0.0f;
	const auto beforeState = original.acquireState();
	const auto afterState = projected->acquireState();
	if(!beforeState || !afterState)
		return 0.0f;
	const auto beforeCount = std::max<int64_t>(0, beforeState->counterAttacks.available());
	const auto afterCount = std::max<int64_t>(0, afterState->counterAttacks.available());
	if(afterCount <= beforeCount)
		return 0.0f;

	std::vector<float> counters;
	for(const auto * enemy : before.battleAdjacentUnits(&original))
	{
		if(!enemy || !enemy->alive() || !enemy->isMeleeAttacker()
			|| before.battleMatchOwner(enemy, &original, true)
			|| !before.isMeleeAttackPossible(enemy, &original))
			continue;
		const auto * projectedEnemy = defended->battleGetUnitByID(enemy->unitId());
		if(!projectedEnemy || !projectedEnemy->alive())
			continue;
		DamageEstimation retaliation;
		defended->battleEstimateDamage(BattleAttackInfo(projectedEnemy, projected, 0, false), &retaliation);
		const auto value = battleDamageValue::averageOrderDamage(retaliation);
		if(value > 0.0f)
			counters.push_back(value);
	}
	std::ranges::sort(counters, std::greater<>());
	const auto first = std::min<int64_t>(beforeCount, counters.size());
	const auto last = std::min<int64_t>(afterCount, counters.size());
	float value = 0.0f;
	for(auto index = first; index < last; ++index)
		value += counters.at(static_cast<size_t>(index));
	return value;
}
}
