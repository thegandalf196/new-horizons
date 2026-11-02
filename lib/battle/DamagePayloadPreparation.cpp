/*
 * DamagePayloadPreparation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "DamagePayloadPreparation.h"
#include "IBattleState.h"
#include "CUnitState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsArchery.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsBattlecraft.h"
#include "NewHorizonsShroud.h"
#include "NewHorizonsBloodrage.h"
#include "NewHorizonsBulwark.h"
#include "../spells/NewHorizonsMagic.h"

namespace
{
constexpr int EXECUTIONER_DAMAGE_PERCENT = 20;
constexpr int ARMOR_PIERCER_DEFENSE_IGNORE_PERCENT = 20;
constexpr int BREAKTHROUGH_DAMAGE_REDUCTION_IGNORE_PERCENT = 50;
constexpr int PIERCING_BOLTS_DEFENSE_IGNORE_PERCENT = 50;
}

const CGHeroInstance * DamagePayloadPreparation::readAttackerHero(const CBattleInfoCallback & callback,
	const BattleAttackInfo & info)
{
	if(!attackerHero.has_value())
		attackerHero = callback.battleGetOwnerHero(info.attacker);
	return *attackerHero;
}

PlayerColor DamagePayloadPreparation::readAttackerOwner(const CBattleInfoCallback & callback,
	const BattleAttackInfo & info)
{
	if(!attackerOwner.has_value())
		attackerOwner = callback.battleGetOwner(info.attacker);
	return *attackerOwner;
}

const CGHeroInstance * DamagePayloadPreparation::readDefenderHero(const CBattleInfoCallback & callback,
	const BattleAttackInfo & info)
{
	if(!defenderHero.has_value())
		defenderHero = callback.battleGetOwnerHero(info.defender);
	return *defenderHero;
}

PlayerColor DamagePayloadPreparation::readDefenderOwner(const CBattleInfoCallback & callback,
	const BattleAttackInfo & info)
{
	if(!defenderOwner.has_value())
		defenderOwner = callback.battleGetOwner(info.defender);
	return *defenderOwner;
}

bool DamagePayloadPreparation::defenderDefended(const BattleAttackInfo & info)
{
	if(!defended.has_value())
		defended = info.defender->defended();
	return *defended;
}

void CBattleInfoCallback::prepareAttackDamageContext(const BattleAttackInfo & info,
	DamagePayloadPreparation & p) const
{
	p.payload.attacker = info.attacker;
	p.payload.defender = info.defender;
	// the script is told where the blow happens rather than left to work it out, so that an
	// attack being weighed reads the same as one being dealt
	p.payload.attackerHex = info.attackerPos.isValid() ? info.attackerPos : info.attacker->getPosition();
	p.payload.defenderHex = info.defenderPos.isValid() ? info.defenderPos : info.defender->getPosition();
	p.payload.chargeDistance = info.chargeDistance;
	p.payload.shooting = info.shooting;
	p.payload.physicalDamage = info.physicalDamage;
	p.payload.activationOutputPercent = battleGetDirectActivationOutputPercent(info);
	p.payload.archeryRangedDamageMultiplierPercent = info.archeryRangedDamageMultiplierPercent;
	if(info.shooting && info.physicalDamage && info.attacker && info.attacker->isBallista())
		p.payload.rangedFollowUpDamagePercent = battleGetRangedFollowUpDamagePercent(info.attacker);
	p.payload.relentlessAssaultDamagePercent = info.relentlessAssaultDamagePercent;
	p.currentBattle = getBattle();
	if(p.currentBattle)
		p.payload.physicalDamageReductionCapPercent = newHorizonsMagic::physicalDamageReductionCapPercent(p.currentBattle->getMagicRules());
	const auto rangedPenetration = battleGetRangedAttackPenetration(info);
	p.payload.rangedDefenseIgnoreBasisPoints = rangedPenetration.creatureDefenseIgnoreBasisPoints;
	p.payload.physicalDamageReductionIgnorePercent = rangedPenetration.physicalDamageReductionIgnorePercent;
	if(p.currentBattle && info.physicalDamage && info.shooting && info.defender
		&& newHorizonsMagic::rulesActive(p.currentBattle->getMagicRules()))
	{
		const auto galeBonuses = info.defender->getBonusesOfType(BonusType::HEAVENLY_GALE);
		if(galeBonuses && !galeBonuses->empty())
		{
			for(const auto & bonus : *galeBonuses)
				p.payload.heavenlyGaleDamageReductionBasisPoints = std::max(
					p.payload.heavenlyGaleDamageReductionBasisPoints, bonus->val);
		}
	}
	p.payload.targetedRangedCommand = battleIsTargetedRangedCommand(
		info.attacker, info.defender, info.shooting, info.secondaryAttack);
	p.payload.targetedRangedCommandPercent = battleTargetedRangedCommandPercent(
		info.attacker, info.defender, info.shooting, info.secondaryAttack);
	if(p.payload.targetedRangedCommandPercent > 0)
	{
		p.causes.attackerOrderCause = HeroCommand::FOCUS_FIRE;
		p.causes.attackerOrderCauses.push_back(HeroCommand::FOCUS_FIRE);
	}
	if(info.shooting && info.physicalDamage && info.attacker && info.defender)
	{
		const auto & adjacentHexes = info.attacker->getSurroundingHexes(p.payload.attackerHex);
		const auto & targetHexes = info.defender->getHexes(p.payload.defenderHex);
		p.payload.archeryAdjacentRangedTarget = std::ranges::any_of(targetHexes, [&adjacentHexes](BattleHex hex)
		{
			return adjacentHexes.contains(hex);
		});
		p.payload.archeryIgnoreAdjacentRangedPenalty = p.payload.archeryAdjacentRangedTarget
			&& newHorizonsArchery::hasPointBlankShot(p.readAttackerHero(*this, info));
		if(p.payload.archeryIgnoreAdjacentRangedPenalty && p.currentBattle)
		{
			const auto & rules = p.currentBattle->getMagicRules();
			if(newHorizonsMagic::rulesActive(rules)
				&& rules["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION)
			{
				const auto & precedence = rules["pointBlankPreservesAdjacentExemption"];
				p.payload.pointBlankPreservesAdjacentExemption = precedence.getType() == JsonNode::JsonType::DATA_BOOL && precedence.Bool();
			}
		}
	}
	if(p.payload.targetedRangedCommand && info.physicalDamage
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.attacker)
		&& newHorizonsArchery::hasTargetCaller(p.readAttackerHero(*this, info)))
	{
		p.payload.targetedRangedCommandPercent += newHorizonsArchery::TARGET_CALLER_DAMAGE_PERCENT;
		p.payload.archeryIgnoreObstaclePenalty = true;
	}
	const auto * archeryHero = info.attacker ? p.readAttackerHero(*this, info) : nullptr;
	if(info.physicalDamage && info.shooting
		&& battleIsCounterBatteryMachineTarget(info.attacker, info.defender))
		p.payload.counterBatteryFinalDamageMultiplier = 150;
	if(info.physicalDamage && info.shooting && info.attacker && info.attacker->isBallista() && info.defender)
	{
		const auto * controllerHero = p.readAttackerHero(*this, info);
		if(controllerHero && controllerHero->hasActivePerk(
			"new-horizons:warMachines", "new-horizons:warMachines.piercingBolts"))
			p.payload.warMachinesPiercingBoltsDefenseIgnorePercent = PIERCING_BOLTS_DEFENSE_IGNORE_PERCENT;
	}
	const bool ordinaryArcheryShot = info.physicalDamage && info.shooting
		&& newHorizonsArchery::isOrdinaryPhysicalShooter(info.attacker) && archeryHero;
	if(ordinaryArcheryShot)
	{
		if(newHorizonsArchery::hasArmorPiercingShot(archeryHero))
			p.payload.archeryRangedDefenseIgnorePercent += newHorizonsArchery::ARMOR_PIERCING_DEFENSE_IGNORE_PERCENT;
		p.payload.archeryHighArc = newHorizonsArchery::hasHighArc(archeryHero);
		if(p.payload.archeryHighArc)
			p.payload.archeryIgnoreObstaclePenalty = true;

		if(newHorizonsArchery::hasDeadeye(archeryHero))
		{
			const auto attackerState = info.attacker->acquireState();
			if(attackerState->archeryDeadeyeRound != battleGetRound())
			{
				p.payload.archeryMaximumCreatureDamage = true;
				p.payload.archeryRangedDefenseIgnorePercent += newHorizonsArchery::DEADEYE_DEFENSE_IGNORE_PERCENT;
			}
		}

		if(newHorizonsArchery::hasCrossfire(archeryHero) && info.defender)
		{
			const auto attackerSide = playerToSide(p.readAttackerOwner(*this, info));
			const auto defenderState = std::dynamic_pointer_cast<battle::CUnitState>(info.defender->acquireState());
			if(defenderState && defenderState->archeryCrossfireAvailable(
				attackerSide, info.attacker->unitId(), battleGetRound()))
				p.payload.archeryCrossfireDamagePercent = newHorizonsArchery::CROSSFIRE_DAMAGE_PERCENT;
		}
	}
	if(p.payload.targetedRangedCommand && p.payload.targetedRangedCommandPercent > 0)
		p.causes.attackerOrderCause = HeroCommand::FOCUS_FIRE;
}

void CBattleInfoCallback::preparePhysicalDamageModifiers(const BattleAttackInfo & info,
	DamagePayloadPreparation & p) const
{
	if(info.physicalDamage && !info.shooting && info.attacker && info.defender
		&& p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info))
	{
		if(const auto * hero = p.readAttackerHero(*this, info))
		{
			const auto hasOffensePerk = [hero](const char * perk)
			{
				return hero->hasActivePerk("new-horizons:offense", perk);
			};
			if(hasOffensePerk("new-horizons:offense.executioner")
				&& info.defender->getAvailableHealth() * 100 < info.defender->getTotalHealth() * 40)
				p.payload.executionerDamagePercent = EXECUTIONER_DAMAGE_PERCENT;
			if(hasOffensePerk("new-horizons:offense.armorPiercer"))
				p.payload.meleeDefenseIgnorePercent = ARMOR_PIERCER_DEFENSE_IGNORE_PERCENT;
			if(hasOffensePerk("new-horizons:offense.breakthrough") && p.defenderDefended(info))
			{
				p.payload.defensiveStanceDamageReductionIgnorePercent = BREAKTHROUGH_DAMAGE_REDUCTION_IGNORE_PERCENT;
				p.payload.defensiveStanceDefenseBonus = std::max(0, info.defender->getDefense(false)
					- info.defender->getDefenseIgnoringDefensiveStance(false));
			}
		}
	}
	// Bloodrage scales ordinary primary elemental shots as well as physical
	// attacks. Actual casts use spell mechanics, never this attack p.payload.
	// Preserve historical pre-v3/absent contexts and existing physical paths.
	const auto & bloodrageRules = getBattle()->getMagicRules();
	const bool elementalBloodrageAttack = !info.secondaryAttack && info.shooting
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.attacker)
		&& info.attacker->unitSlot() != SlotID::WAR_MACHINES_SLOT
		&& info.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		&& newHorizonsMagic::rulesActive(bloodrageRules)
		&& bloodrageRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	if(info.physicalDamage || elementalBloodrageAttack)
		p.payload.bloodrageDamagePercent = battleGetBloodrageDamagePercent(info.attacker, info.defender);
	if(info.physicalDamage)
	{
		if(info.defender && newHorizonsCombatSkills::isPhysicalCreatureAttack(info.attacker, info.physicalDamage)
			&& p.readAttackerOwner(*this, info) != p.readDefenderOwner(*this, info)
			&& newHorizonsBloodrage::hasAvatarOfRage(p.readAttackerHero(*this, info)))
		{
			const auto side = playerToSide(p.readAttackerOwner(*this, info));
			if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
				&& newHorizonsBloodrage::atMaximumForAttack(p.payload.bloodrageDamagePercent,
					getBattle()->getBloodrageCapPercent(side)))
			{
				// Blood Scent's higher Rage belongs to this attack only. Reuse the
				// target Creature Defense channels, never hero attributes or Frenzy.
				if(info.shooting)
					p.payload.archeryRangedDefenseIgnorePercent += newHorizonsBloodrage::AVATAR_DEFENSE_IGNORE_PERCENT;
				else
					p.payload.meleeDefenseIgnorePercent += newHorizonsBloodrage::AVATAR_DEFENSE_IGNORE_PERCENT;
			}
		}
		const bool ordinaryCreatureAttack = newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.attacker);
		if(info.shooting && ordinaryCreatureAttack)
			p.payload.newHorizonsArcheryDamagePercent = newHorizonsCombatSkills::archeryDamagePercent(
				p.readAttackerHero(*this, info));
		if(ordinaryCreatureAttack && info.attacker->battlecraftWaitBonusAvailable())
			p.payload.battlecraftWaitDamagePercent = newHorizonsBattlecraft::waitDamagePercent(
				p.readAttackerHero(*this, info), dynamic_cast<const battle::CUnitState *>(info.attacker));
		if(ordinaryCreatureAttack)
			p.payload.newHorizonsArmorerReductionPercent = newHorizonsCombatSkills::armorerReductionPercent(
				p.readDefenderHero(*this, info));
		if(info.defender && battleHasFormationFightingProtection(info.defender, info.defenderPos))
			p.payload.formationFightingReductionPercent = newHorizonsCombatSkills::formationFightingReductionPercent(
				p.readDefenderHero(*this, info));
		if(ordinaryCreatureAttack && info.defender && p.defenderDefended(info))
		{
			p.payload.battlecraftDefendReductionPercent = newHorizonsBattlecraft::defendReductionPercent(
				p.readDefenderHero(*this, info), dynamic_cast<const battle::CUnitState *>(info.defender));
			if(info.shooting)
				p.payload.paviseDamageReductionPercent = newHorizonsCombatSkills::paviseReductionPercent(
					p.readDefenderHero(*this, info));
		}
		if(newHorizonsCombatSkills::isPhysicalCreatureAttack(info.attacker, info.physicalDamage)
			&& battleHasBastionProtection(info.defender))
			p.payload.armorerBastionFinalDamageMultiplier = newHorizonsCombatSkills::BASTION_FINAL_DAMAGE_MULTIPLIER;
		if(battleIsShroudFlankingAttack(info))
		{
			const auto * attackerHero = p.readAttackerHero(*this, info);
			p.payload.shroudFlankingDamagePercent = newHorizonsShroud::flankingDamagePercent(
				newHorizonsShroud::rank(attackerHero))
				+ newHorizonsShroud::backstabDamagePercent(attackerHero)
				+ newHorizonsShroud::ambusherDamagePercent(attackerHero, info.attacker);
			p.payload.shroudFlankingDamagePercent += battleEncircledDoomDamagePercent(info);
			p.payload.meleeDefenseIgnorePercent += newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
				attackerHero, info.defender, playerToSide(p.readAttackerOwner(*this, info)));
		}
		if(info.defender && p.defenderDefended(info) && ordinaryCreatureAttack
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(info.defender)
			&& (!info.shooting || !info.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)))
		{
			const auto reductionForHero = [this](const CGHeroInstance * hero)
			{
				const auto terrain = getBattle()->getTerrainType();
				const bool mireTerrain = newHorizonsBulwark::hasMireborn(hero)
					&& (terrain == TerrainId::SWAMP || terrain == TerrainId::ROUGH);
				return newHorizonsBulwark::reductionBasisPoints(
					newHorizonsBulwark::rank(hero), hero ? hero->getPrimSkillLevel(PrimarySkill::DEFENSE) : 0, mireTerrain);
			};
			const auto * hero = p.readDefenderHero(*this, info);
			const int baseReduction = reductionForHero(hero);
			const auto defenderState = info.defender->acquireState();
			if(newHorizonsBulwark::hasImmovable(hero) && defenderState
				&& defenderState->bulwarkImmovableRound != battleGetRound())
				p.payload.bulwarkImmovableFinalDamageMultiplier = 75;
			p.payload.bulwarkDamageReductionBasisPoints = baseReduction;
			if(baseReduction > 0 && newHorizonsBulwark::hasSharedCover(hero))
			{
				const auto friends = battleAdjacentUnits(info.defender);
				for(const auto * friendUnit : friends)
				{
					if(friendUnit->unitSide() != info.defender->unitSide() || !friendUnit->defended()
						|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(friendUnit))
						continue;
					p.payload.bulwarkDamageReductionBasisPoints = std::min(10000,
						baseReduction + newHorizonsBulwark::sharedCoverBasisPoints(baseReduction));
					break;
				}
			}
		}
	}
	p.payload.shroudDeepFlankDamagePercent = battleDeepFlankDamagePercent(info);
	if(info.preemptiveDamagePercent > 0)
		p.payload.preemptiveDamageMultiplier = info.preemptiveDamagePercent;
	if(info.cleaveDamagePercent > 0)
		p.payload.cleaveFinalDamageMultiplier = info.cleaveDamagePercent;
}
