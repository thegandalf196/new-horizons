/*
 * DamageOrderPayload.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsCommandResources.h"
#include "CBattleInfoCallback.h"
#include "DamagePayloadPreparation.h"
#include "IBattleState.h"
#include "CUnitState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsArchery.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsDivineMandate.h"
#include "PassiveDivineMandate.h"

namespace
{
constexpr int SHOCK_ASSAULT_DEFENSE_IGNORE_PERCENT = 25;

bool eligibleDamageOrderUnit(const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && !unit->isTurret()
		&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}
}

void CBattleInfoCallback::prepareDamageOrders(const BattleAttackInfo & info,
	DamagePayloadPreparation & p) const
{
	if(newHorizonsCommand::unbrokenRanks(*this, info.defender))
		p.payload.commandBeneficiaryDamageMultiplierPercent = 80;
	if(heroCommands::isCanonicalRules(getBattle()->getHeroCommandRules())
		&& info.attacker && info.defender && !info.attacker->isGhost() && !info.defender->isGhost())
	{
		const auto & rules = getBattle()->getHeroCommandRules()["commands"];
		const auto attack = p.readAttackerHero(*this, info);
		const auto defend = p.readDefenderHero(*this, info);
		const auto attackerSide = playerToSide(p.readAttackerOwner(*this, info));
		const auto defenderSide = playerToSide(p.readDefenderOwner(*this, info));
		const auto attackerStates = battleGetHeroOrderStates(attackerSide);
		const auto defenderStates = battleGetHeroOrderStates(defenderSide);
		if(p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info))
		{
			const auto attackerRiders = newHorizonsPassiveDivineMandate::activeOrderRiders(*this, info.attacker, attackerStates);
			const auto defenderRiders = newHorizonsPassiveDivineMandate::activeOrderRiders(*this, info.defender, defenderStates);
			if(!info.shooting)
			{
				p.payload.heroOrderDamagePercent += attackerRiders.meleeDamagePercent;
				p.payload.meleeDefenseIgnorePercent += attackerRiders.meleeDefenseIgnorePercent;
			}
			if(defenderRiders.enemyAttackReductionPercent != 0)
			{
				p.payload.heroOrderDamageReductionPercent = defenderRiders.enemyAttackReductionPercent;
				if(info.physicalDamage)
					p.payload.heroOrderDamageReductionPercents.push_back(defenderRiders.enemyAttackReductionPercent);
			}
		}
		prepareAttackerDamageOrders(info, p, rules, attack, attackerSide, attackerStates);
		prepareDefenderDamageOrders(info, p, rules, defend, defenderStates);
	}
}

void CBattleInfoCallback::prepareAttackerDamageOrders(const BattleAttackInfo & info,
	DamagePayloadPreparation & p, const JsonNode & rules, const CGHeroInstance * attack,
	BattleSide attackerSide, const std::vector<HeroOrderState> & attackerStates) const
{
	const auto recordAttackerCause = [&](HeroCommand command)
	{
		p.causes.attackerOrderCauses.push_back(command);
		if(p.causes.attackerOrderCause == HeroCommand::NONE)
			p.causes.attackerOrderCause = command;
	};
	for(const auto & attackerState : attackerStates)
	{
		if(!attackerState.scheduledFor(info.attacker->unitId(), battleGetRound()) || !attack)
			continue;
		switch(attackerState.command)
		{
		case HeroCommand::FOCUS_FIRE:
			if(eligibleDamageOrderUnit(info.attacker) && info.attacker->isMeleeAttacker()
				&& !info.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
				&& info.physicalDamage && !info.shooting && !info.secondaryAttack
				&& info.defender->alive() && p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info)
				&& attackerState.primaryTargetUnitId == info.defender->unitId()
				&& battleIsFocusFireTargetActive(attackerSide))
			{
				const auto mark = battleGetFocusFireState(attackerSide);
				if(mark && attackerState.scheduledFor(info.attacker->unitId(), battleGetRound())
					&& mark->targetUnitId == info.defender->unitId()
					&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), info.attacker->unitId()))
				{
					const auto combinedArmsDamagePercent = heroCommands::combinedArmsFocusFirePercent(
						(attackerState.crownAndAltarAppliesTo(info.attacker->unitId())
							? attackerState.crownAndAltarFocusFirePercent : mark->rangedDamagePercent), *attack);
					p.payload.combinedArmsDamagePercent += combinedArmsDamagePercent;
					if(combinedArmsDamagePercent > 0)
						recordAttackerCause(HeroCommand::FOCUS_FIRE);
				}
			}
			break;
		case HeroCommand::CHARGE:
			if(eligibleDamageOrderUnit(info.attacker) && !info.shooting && !info.secondaryAttack && info.chargeDistance >= 3
				&& !attackerState.containsConsumed(info.attacker->unitId()))
			{
				const int intrinsic = heroCommandCoefficient(rules["charge"]["effects"]["meleeDamagePercent"],
					*attack, attackerState.divineMandateEfficiencyBonusPercent());
				const int orderDamagePercent = heroCommands::orderPotency(intrinsic + 2 * (info.chargeDistance - 3),
					heroOrderCoefficient(rules["charge"]["effects"]["meleeDamagePercent"], attack, &attackerState, info.attacker->unitId(), false)
						+ 2 * (info.chargeDistance - 3), attackerState, getBattle()->getMagicRules())
					+ (info.physicalDamage
						? heroCommands::revisedPerkBonus(getBattle()->getHeroCommandRules(), attack, HeroCommand::CHARGE) : 0);
				p.payload.heroOrderDamagePercent += orderDamagePercent;
				if(orderDamagePercent > 0)
					recordAttackerCause(HeroCommand::CHARGE);
				if(info.physicalDamage && p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info)
					&& attack->hasActivePerk("new-horizons:offense", "new-horizons:offense.shockAssault"))
					p.payload.chargeDefenseIgnorePercent = SHOCK_ASSAULT_DEFENSE_IGNORE_PERCENT;
			}
			break;
		case HeroCommand::RIPOSTE:
			if(eligibleDamageOrderUnit(info.attacker) && info.retaliation && !info.shooting)
			{
				const int orderDamagePercent = heroOrderCoefficient(rules["riposte"]["effects"]["retaliationDamagePercent"], attack, &attackerState, info.attacker->unitId());
				p.payload.heroOrderDamagePercent += orderDamagePercent;
				if(orderDamagePercent > 0)
					recordAttackerCause(HeroCommand::RIPOSTE);
			}
			break;
		case HeroCommand::BRACE:
			if(eligibleDamageOrderUnit(info.attacker) && info.bracePreemptive && !info.shooting)
			{
				const int ordinary = heroOrderCoefficient(rules["brace"]["effects"]["preemptiveDamagePercent"],
					attack, &attackerState, info.attacker->unitId(), false);
				const int companion = newHorizonsCombatSkills::bracePreemptivePercent(ordinary, attack) - ordinary;
				const int intrinsic = heroCommandCoefficient(rules["brace"]["effects"]["preemptiveDamagePercent"],
					*attack, attackerState.divineMandateEfficiencyBonusPercent());
				const int orderMultiplier = heroCommands::orderPotency(intrinsic, ordinary,
					attackerState, getBattle()->getMagicRules()) + companion;
				p.payload.heroOrderFinalDamageMultipliers.push_back(orderMultiplier);
				if(p.payload.heroOrderFinalDamageMultiplier == 100)
					p.payload.heroOrderFinalDamageMultiplier = orderMultiplier;
				recordAttackerCause(HeroCommand::BRACE);
			}
			break;
		case HeroCommand::FLANK:
			if(eligibleDamageOrderUnit(info.attacker)
				&& attackerState.primaryTargetUnitId == info.defender->unitId())
			{
				if(!info.shooting)
				{
					const int orderDamagePercent = battleHeroOrderFlankMeleeDamagePercent(info);
					p.payload.heroOrderDamagePercent += orderDamagePercent;
					if(orderDamagePercent > 0)
						recordAttackerCause(HeroCommand::FLANK);
				}
				else if(info.shooting && info.physicalDamage && info.defender->alive()
					&& p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info)
					&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker)
					&& attackerState.flankFor(info.defender->unitId()))
				{
					const auto combinedArmsDamagePercent = heroCommands::combinedArmsFlankPercent(
						newHorizonsDivineMandate::crownOrderFormula(rules["flank"]["effects"]["meleeDamagePercent"],
							attackerState.crownAndAltarAppliesTo(info.attacker->unitId())), *attack,
						attackerState.divineMandateEfficiencyBonusPercent());
					p.payload.combinedArmsDamagePercent += combinedArmsDamagePercent;
					if(combinedArmsDamagePercent > 0)
						recordAttackerCause(HeroCommand::FLANK);
				}
			}
			break;
		case HeroCommand::SECOND_WIND:
			if(attackerState.secondWindActive && attackerState.primaryTargetUnitId == info.attacker->unitId())
			{
				const int orderMultiplier = heroCommands::orderPotency(
					heroCommands::secondWindPercent(*attack,
						attackerState.divineMandateEfficiencyBonusPercent()),
					newHorizonsDivineMandate::crownSecondWindPercent(*attack, attackerState, info.attacker->unitId()),
					attackerState, getBattle()->getMagicRules());
				if(orderMultiplier != 100)
				{
					p.payload.heroOrderFinalDamageMultipliers.push_back(orderMultiplier);
					if(p.payload.heroOrderFinalDamageMultiplier == 100)
						p.payload.heroOrderFinalDamageMultiplier = orderMultiplier;
					recordAttackerCause(HeroCommand::SECOND_WIND);
				}
			}
			break;
		default:
			break;
		}
	}
}

void CBattleInfoCallback::prepareDefenderDamageOrders(const BattleAttackInfo & info,
	DamagePayloadPreparation & p, const JsonNode & rules, const CGHeroInstance * defend,
	const std::vector<HeroOrderState> & defenderStates) const
{
	const auto recordDefenderCause = [&](HeroCommand command)
	{
		p.causes.defenderOrderCauses.push_back(command);
		if(p.causes.defenderOrderCause == HeroCommand::NONE)
			p.causes.defenderOrderCause = command;
	};
	for(const auto & defenderState : defenderStates)
	{
		if(!defenderState.scheduledFor(info.defender->unitId(), battleGetRound()) || !defend
			|| !eligibleDamageOrderUnit(info.defender) || !info.physicalDamage)
			continue;
		int orderDamageReductionPercent = 0;
		switch(defenderState.command)
		{
		case HeroCommand::FEINT:
			if(info.retaliation && !info.shooting && !info.secondaryAttack && !info.bracePreemptive
				&& defenderState.revision
				&& p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info))
				orderDamageReductionPercent = heroCommands::revisedFeintReductionFor(defenderState, info.defender->unitId());
			break;
		case HeroCommand::RIPOSTE:
			if(!info.shooting)
				orderDamageReductionPercent = heroOrderCoefficient(rules["riposte"]["effects"]["meleeDamageReductionPercent"], defend, &defenderState, info.defender->unitId());
			break;
		case HeroCommand::HOLD_THE_LINE:
			if(battleIsHoldTheLineRecipient(defenderState, info.defender))
				orderDamageReductionPercent = heroOrderCoefficient(rules["holdTheLine"]["effects"]["damageReductionPercent"], defend, &defenderState, info.defender->unitId())
					+ heroCommands::revisedPerkBonus(getBattle()->getHeroCommandRules(), defend, HeroCommand::HOLD_THE_LINE);
			break;
		case HeroCommand::PROTECT:
			if(!info.shooting && info.protectIntercepted
				&& defenderState.primaryTargetUnitId == info.defender->unitId())
				orderDamageReductionPercent = heroOrderCoefficient(rules["protect"]["effects"]["interceptedDamageReductionPercent"], defend, &defenderState, info.defender->unitId());
			break;
		default:
			break;
		}
		if(orderDamageReductionPercent > 0)
		{
			if(p.payload.heroOrderDamageReductionPercent == 0)
				p.payload.heroOrderDamageReductionPercent = orderDamageReductionPercent;
			p.payload.heroOrderDamageReductionPercents.push_back(orderDamageReductionPercent);
			recordDefenderCause(defenderState.command);
		}
	}
}
