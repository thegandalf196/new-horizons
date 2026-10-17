/*
 * CanonicalOrderValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CanonicalOrderValue.h"
#include "BattleDamageValue.h"
#include "HeroOrderCandidateRules.h"
#include "SecondWindOpportunity.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace heroOrderValue
{
using heroOrderCandidates::riposteCommand;
using heroOrderCandidates::braceCommand;
using heroOrderCandidates::protectCommand;
using heroOrderCandidates::flankCommand;
using heroOrderCandidates::secondWindCommand;
using heroOrderCandidates::isEligibleOrderUnit;
using battleDamageValue::averageOrderDamage;

CanonicalOrderValue::CanonicalOrderValue(const CBattleInfoCallback & battle, BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetIds)
	: battle(battle), side(side), command(command), targetIds(targetIds)
{
	const auto perspective = battle.battleGetMySide();
	const bool heroKnown = perspective == BattleSide::ALL_KNOWING || perspective == side;
	hero = heroKnown ? battle.battleGetFightingHero(side) : nullptr;
	units = battle.battleGetAllUnits(false);
	ownPlayer = battle.sideToPlayer(side);
	for(const auto * unit : units)
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
			continue;
		if(battle.battleGetOwner(unit) == ownPlayer)
			ownUnits.push_back(unit);
		else
			enemyUnits.push_back(unit);
	}

	commandRules = & battle.getBattle()->getHeroCommandRules()["commands"];
	combinedArmsEnabled = hero
		&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		&& heroCommands::hasCombinedArms(hero);
	const auto round = battle.battleGetRound();
	orderAllowance = battle.battleGetOrderActionAllowance(side);
	preparedOrder = hero
		? battle.battlePrepareHeroOrderState(side, command, targetIds)
		: std::optional<HeroOrderState>();
	warcastingBonus = hero && orderAllowance
		&& orderAllowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO
		&& newHorizonsWarcasting::enabled(battle.getBattle()->getMagicRules())
		? newHorizonsWarcasting::orderBonus(hero, battle.getBattle()->getWarcastingState(side), round) : 0;
	divineMandateEfficiencyBonusPercent = preparedOrder
		? preparedOrder->divineMandateEfficiencyBonusPercent() : 0;
}

float CanonicalOrderValue::coefficient(const char * commandKey, const char * effectKey) const
{
	const auto & formula = (*commandRules)[commandKey]["effects"][effectKey];
	return static_cast<float>(hero ? heroCommands::coefficient(formula, *hero, warcastingBonus,
		divineMandateEfficiencyBonusPercent)
		: heroCommands::coefficient(formula, 0, 0));
}

float CanonicalOrderValue::meleeDamage(const battle::Unit * attackerUnit, const battle::Unit * defenderUnit) const
{
	if(!attackerUnit || !defenderUnit || !attackerUnit->isMeleeAttacker())
		return 0.0f;
	return averageOrderDamage(battle.battleEstimateDamage(
		BattleAttackInfo(attackerUnit, defenderUnit, 0, false)));
}

float CanonicalOrderValue::anyDamage(const battle::Unit * attackerUnit, const battle::Unit * defenderUnit) const
{
	if(!attackerUnit || !defenderUnit)
		return 0.0f;
	const bool shooting = battle.battleCanShoot(attackerUnit, defenderUnit->getPosition());
	return averageOrderDamage(battle.battleEstimateDamage(
		BattleAttackInfo(attackerUnit, defenderUnit, 0, shooting)));
}

float CanonicalOrderValue::bestOwnMeleeDamage(const battle::Unit * defenderUnit) const
{
	float best = 0.0f;
	for(const auto * unit : ownUnits)
		best = std::max(best, meleeDamage(unit, defenderUnit));
	return best;
}

float CanonicalOrderValue::bestEnemyMeleeDamage(const battle::Unit * defenderUnit) const
{
	float best = 0.0f;
	for(const auto * unit : enemyUnits)
		best = std::max(best, meleeDamage(unit, defenderUnit));
	return best;
}

float CanonicalOrderValue::sumOwnMeleeDamage() const
{
	float total = 0.0f;
	for(const auto * own : ownUnits)
	{
		float best = 0.0f;
		for(const auto * enemy : enemyUnits)
			best = std::max(best, meleeDamage(own, enemy));
		total += best;
	}
	return total;
}

float CanonicalOrderValue::focusFire(std::optional<int> focusFireSnapshotPercent) const
{
	const auto * target = battle.battleGetUnitByID(targetIds.front());
	if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
		return 0.0f;
	// The prepared snapshot is shared with the runtime, so Combined Arms uses
	// precisely half of the Order's ranged coefficient (not a recomputed
	// shooter-only extension such as Target Caller).
	const auto rangedPercent = focusFireSnapshotPercent.value_or(
		static_cast<int>(coefficient("focusFire", "rangedDamagePercent")));
	const auto combinedArmsMeleePercent = combinedArmsEnabled
		? static_cast<float>(heroCommands::combinedArmsFocusFirePercent(rangedPercent, *hero)) : 0.0f;
	float potential = 0.0f;
	for(const auto * unit : ownUnits)
	{
		if(!unit->willMove(0))
			continue;
		const bool canReceiveRangedBonus = unit->isShooter() && !unit->isTurret()
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& battle.battleCanShoot(unit, target->getPosition());
		const float rangedValue = canReceiveRangedBonus
			? anyDamage(unit, target) * static_cast<float>(rangedPercent) / 100.0f : 0.0f;
		const bool canReceiveMeleeBonus = combinedArmsMeleePercent > 0.0f
			&& isEligibleOrderUnit(battle, side, unit)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& unit->isMeleeAttacker();
		const float meleeValue = canReceiveMeleeBonus
			? meleeDamage(unit, target) * combinedArmsMeleePercent / 100.0f : 0.0f;
		// A stack chooses one attack mode during its activation.  If a shooter
		// can also make a melee attack, value the better Focus Fire benefit rather
		// than counting the same stack twice.
		potential += std::max(rangedValue, meleeValue);
	}
	return potential;
}

float CanonicalOrderValue::charge(float ownMeleePotential, float chargePercent) const
{
	const bool canCharge = std::any_of(ownUnits.begin(), ownUnits.end(), [](const auto * unit)
	{
		return unit->isMeleeAttacker() && unit->getMovementRange(0) >= 3;
	});
	return canCharge ? ownMeleePotential * chargePercent / 100.0f : 0.0f;
}

float CanonicalOrderValue::riposte(float riposteReduction, float riposteDamage) const
{
	float incoming = 0.0f;
	float retaliation = 0.0f;
	float vengeanceRetaliation = 0.0f;
	static const auto firstStrikeSelector = Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	for(const auto * own : ownUnits)
		for(const auto * enemy : enemyUnits)
		{
			if(!enemy->isMeleeAttacker())
				continue;
			const BattleAttackInfo incomingAttack(enemy, own, 0, false);
			DamageEstimation retaliationEstimate;
			const auto incomingEstimate = battle.battleEstimateDamage(incomingAttack, &retaliationEstimate);
			incoming = std::max(incoming, averageOrderDamage(incomingEstimate));
			retaliation = std::max(retaliation, averageOrderDamage(retaliationEstimate));
			if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE)
				&& isEligibleOrderUnit(battle, side, own)
				&& own->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
				&& !own->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS)
				&& !own->isTimeStopped()
				&& !own->hasBonusOfType(BonusType::NO_RETALIATION)
				&& !enemy->hasBonusOfType(BonusType::BLOCKS_RETALIATION)
				&& !enemy->isInvincible()
				&& !battle.isLongWeaponAttack(enemy, own)
				&& battle.isMeleeAttackPossible(enemy, own)
				&& (!battle.battleShroudDeniesRetaliation(incomingAttack)
					|| own->hasBonus(firstStrikeSelector))
				&& !newHorizonsOffense::hasVengeanceRetaliationBonus(own))
			{
				// Vengeance supplies the charge that ableToRetaliate() would otherwise
				// reject after a spent counter. Mirror the engine's non-ammunition
				// restrictions above, then estimate from detached post-hit states.
				const auto counterAfterMinimumDamage = estimateExtraCounter(own, enemy, incomingEstimate.damage.min);
				const auto counterAfterMaximumDamage = estimateExtraCounter(own, enemy, incomingEstimate.damage.max);
				DamageEstimation projectedRetaliation;
				projectedRetaliation.damage.min = std::min(counterAfterMinimumDamage.damage.min,
					counterAfterMaximumDamage.damage.min);
				projectedRetaliation.damage.max = std::max(counterAfterMinimumDamage.damage.max,
					counterAfterMaximumDamage.damage.max);
				projectedRetaliation.kills.min = std::min(counterAfterMinimumDamage.kills.min,
					counterAfterMaximumDamage.kills.min);
				projectedRetaliation.kills.max = std::max(counterAfterMinimumDamage.kills.max,
					counterAfterMaximumDamage.kills.max);
				vengeanceRetaliation = std::max(vengeanceRetaliation,
					averageOrderDamage(projectedRetaliation));
			}
		}
	const float extraRetaliation = vengeanceRetaliation * (100.0f + riposteDamage) / 100.0f;
	return incoming * riposteReduction / 100.0f
		+ retaliation * riposteDamage / 100.0f + extraRetaliation;
}

float CanonicalOrderValue::brace(float braceDamage) const
{
	float advancingDamage = 0.0f;
	for(const auto * enemy : enemyUnits)
		if(enemy->isMeleeAttacker() && enemy->getMovementRange(0) >= 3)
			for(const auto * own : ownUnits)
				// Brace is a pre-emptive blow by the defending friendly stack,
				// not a multiplier on the advancing enemy's own attack.
				advancingDamage = std::max(advancingDamage, meleeDamage(own, enemy));
	const auto resolvedBraceDamage = newHorizonsCombatSkills::bracePreemptivePercent(braceDamage, hero);
	return advancingDamage * resolvedBraceDamage / 100.0f;
}

float CanonicalOrderValue::protect(float protectReduction) const
{
	const auto * protector = battle.battleGetUnitByID(targetIds[0]);
	const auto * ward = battle.battleGetUnitByID(targetIds[1]);
	if(!isEligibleOrderUnit(battle, side, protector) || !isEligibleOrderUnit(battle, side, ward))
		return 0.0f;
	return bestEnemyMeleeDamage(ward) * protectReduction / 100.0f;
}

float CanonicalOrderValue::flank(float flankDamage, float combinedArmsRangedPercent) const
{
	const auto * target = battle.battleGetUnitByID(targetIds.front());
	if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
		return 0.0f;
	const bool targetCanBeFlanked = !battle.battleHasFormationFightingProtection(target);
	// Estimate the immediate opportunity from allied melee stacks that have not
	// acted yet. The authoritative attack path records exact sides as attacks
	// resolve; this projection reads current contacts only and never mutates them.
	uint8_t availableSides = 0;
	for(const auto * unit : ownUnits)
	{
		if(!targetCanBeFlanked || !unit->isMeleeAttacker() || !unit->willMove(0))
			continue;
		availableSides |= battle.battleHeroOrderFlankSide(unit, target);
	}
	int distinctSides = 0;
	for(auto bits = availableSides; bits; bits &= static_cast<uint8_t>(bits - 1))
		++distinctSides;
	const int additionalSides = std::max(0, distinctSides - 1);
	const int additionalSidePercent = battle.battleHeroOrderFlankAdditionalSidePercent(
		side, warcastingBonus, divineMandateEfficiencyBonusPercent);
	const auto meleeOrderValue = targetCanBeFlanked
		? bestOwnMeleeDamage(target) * (flankDamage + additionalSides * additionalSidePercent) / 100.0f
		: 0.0f;
	if(combinedArmsRangedPercent <= 0.0f)
		return meleeOrderValue;

	const battle::Unit * bestMeleeAttacker = nullptr;
	float bestMeleeDamage = 0.0f;
	for(const auto * unit : ownUnits)
	{
		const auto value = meleeDamage(unit, target);
		if(value > bestMeleeDamage)
		{
			bestMeleeDamage = value;
			bestMeleeAttacker = unit;
		}
	}
	float rangedOpportunity = 0.0f;
	float overlappingMeleeAttackerRangedOpportunity = 0.0f;
	for(const auto * unit : ownUnits)
	{
		if(!isEligibleOrderUnit(battle, side, unit)
			|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| !unit->willMove(0) || !newHorizonsArchery::isOrdinaryPhysicalShooter(unit)
			|| !battle.battleCanShoot(unit, target->getPosition()))
			continue;
		const auto opportunity = anyDamage(unit, target) * combinedArmsRangedPercent / 100.0f;
		if(unit == bestMeleeAttacker)
			overlappingMeleeAttackerRangedOpportunity = opportunity;
		else
			rangedOpportunity += opportunity;
	}
	// The leading melee attacker cannot both shoot and strike in the same
	// activation. Preserve its existing melee valuation and only add ranged
	// value where that alternative is better; other shooters remain separate
	// future attacks.
	rangedOpportunity += std::max(0.0f,
		overlappingMeleeAttackerRangedOpportunity - meleeOrderValue);
	return meleeOrderValue + rangedOpportunity;
}

float CanonicalOrderValue::secondWind(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & projectionSource) const
{
	const auto * target = battle.battleGetUnitByID(targetIds.front());
	const bool canonicalRules = heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
	const bool hasSpentActivation = target
		&& (target->moved(0) || (canonicalRules && target->defended()));
	if(!isEligibleOrderUnit(battle, side, target) || !hasSpentActivation)
		return 0.0f;
	const auto activation = projectSecondWindActivation(battle, environment, projectionSource,
		side, target->unitId());
	float extraAttack = 0.0f;
	for(const auto * enemy : enemyUnits)
		if(!activation || hasSecondWindAttackOpportunity(*activation, target->unitId(), enemy->unitId()))
			extraAttack = std::max(extraAttack, anyDamage(target, enemy));
	const auto directDamagePercent = hero
		? static_cast<float>(heroCommands::secondWindPercent(*hero, warcastingBonus,
			divineMandateEfficiencyBonusPercent)) : 50.0f;
	return extraAttack * directDamagePercent / 100.0f;
}

DamageEstimation CanonicalOrderValue::estimateExtraCounter(const battle::Unit * own,
	const battle::Unit * enemy, int64_t projectedDamage) const
{
	auto projectedOwn = own->acquireState();
	projectedOwn->damage(projectedDamage);
	if(!projectedOwn->alive())
		return DamageEstimation();
	BattleAttackInfo extraRetaliation(projectedOwn.get(), enemy, 0, false);
	extraRetaliation.retaliation = true;
	return battle.battleEstimateDamage(extraRetaliation);
}

DirectOrderValue CanonicalOrderValue::directValue(std::optional<int> focusFireSnapshotPercent,
	const Environment * environment, const std::shared_ptr<CBattleInfoCallback> & projectionSource) const
{
	const auto ownMeleePotential = sumOwnMeleeDamage();
	if(command == HeroCommand::FOCUS_FIRE && targetIds.size() == 1)
		return {focusFire(focusFireSnapshotPercent)};
	const auto chargePercent = coefficient("charge", "meleeDamagePercent");
	const auto holdPercent = coefficient("holdTheLine", "damageReductionPercent");
	const auto riposteReduction = coefficient("riposte", "meleeDamageReductionPercent");
	const auto riposteDamage = coefficient("riposte", "retaliationDamagePercent");
	const auto braceDamage = coefficient("brace", "preemptiveDamagePercent");
	const auto protectReduction = coefficient("protect", "interceptedDamageReductionPercent");
	const auto flankDamage = coefficient("flank", "meleeDamagePercent");
	const auto & flankFormula = (*commandRules)["flank"]["effects"]["meleeDamagePercent"];
	const auto combinedArmsRangedPercent = combinedArmsEnabled
		? static_cast<float>(heroCommands::combinedArmsFlankPercent(flankFormula, *hero, warcastingBonus,
			divineMandateEfficiencyBonusPercent))
		: 0.0f;
	if(command == HeroCommand::CHARGE)
		return {charge(ownMeleePotential, chargePercent)};
	if(command == HeroCommand::HOLD_THE_LINE)
		return {std::nullopt, holdPercent};
	if(command == riposteCommand())
		return {riposte(riposteReduction, riposteDamage)};
	if(command == braceCommand())
		return {brace(braceDamage)};
	if(command == protectCommand() && targetIds.size() == 2)
		return {protect(protectReduction)};
	if(command == flankCommand() && targetIds.size() == 1)
		return {flank(flankDamage, combinedArmsRangedPercent)};
	if(command == secondWindCommand() && targetIds.size() == 1)
		return {secondWind(environment, projectionSource)};
	return {0.0f};
}
}
