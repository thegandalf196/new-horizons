/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "BattleEvaluator.h"
#include "NewHorizonsHexOfPain.h"
#include "../../lib/CStack.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsShroud.h"

#include <tbb/parallel_for.h>

namespace
{
bool hasNightProwlerBonus(const battle::Unit * unit)
{
	return unit && unit->hasBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
}

bool projectNoQuarterAfterHit(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	StackWithBonuses & target)
{
	if(!battle.battleCanTriggerNoQuarter(attack) || !target.alive() || target.isTimeStopped()
		|| !battle.battleMatchOwner(attack.attacker, &target)
		|| !newHorizonsOffense::belowNoQuarterThreshold(
			target.getAvailableHealth(), battle::getMaximumHealth(target)))
		return false;

	const int32_t moraleActivations = battle.getBattle()->getActiveStackID()
		== static_cast<int32_t>(target.unitId()) ? 2 : 1;
	target.applyNoQuarter(moraleActivations, true);
	return true;
}

std::optional<uint32_t> relentlessAssaultPrimaryTargetId(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack)
{
	const auto * attacker = attack.attacker;
	const auto * defender = attack.defender;
	if(!attacker || !defender || !attack.physicalDamage || attack.retaliation
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| attack.cleaveDamagePercent > 0 || attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
		return {};
	const auto side = attacker->unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT)
		|| !attacker->alive() || attacker->isGhost() || attacker->isTurret()
		|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return {};
	const auto * primaryTarget = battle.battleResolveHeroOrderTarget(attacker, defender, attack.shooting);
	if(!primaryTarget || !primaryTarget->alive() || primaryTarget->isGhost() || primaryTarget->isTurret()
		|| primaryTarget->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| primaryTarget->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| !battle.battleMatchOwner(attacker, primaryTarget))
		return {};
	return primaryTarget->unitId();
}

void projectRelentlessAssaultAttack(HypotheticBattle & battle, const BattleAttackInfo & attack,
	std::optional<uint32_t> primaryTargetUnitId)
{
	if(!attack.attacker || !primaryTargetUnitId)
		return;
	battle.recordRelentlessAssaultAttack(attack.attacker->unitSide(), *primaryTargetUnitId);
}

uint32_t distToNearestReachableNeighbour(const ReachabilityInfo & reachability,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	auto attackableHexes = defender->getHexes();
	if(attacker->doubleWide())
	{
		if(defender->doubleWide())
			attackableHexes.insert(battle::Unit::getHexes(defender->occupiedHex(), true, defender->unitSide()));
		else
			attackableHexes.insert(battle::Unit::getHexes(defender->getPosition(), true, defender->unitSide()));
	}
	attackableHexes.eraseIf([defender](const BattleHex & hex)
		{
			return hex.getY() != defender->getPosition().getY() || !hex.isAvailable();
		});

	uint32_t result = ReachabilityInfo::INFINITE_DIST;
	for(const auto & targetHex : attackableHexes)
		for(const auto & neighbour : targetHex.getNeighbouringTiles())
			if(reachability.isReachable(neighbour))
				result = std::min(result, reachability.distances.at(neighbour.toInt()));
	return result;
}

bool controlledByDifferentPlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second)
{
	const auto firstOwner = battle.battleGetOwner(first);
	const auto secondOwner = battle.battleGetOwner(second);
	return firstOwner != PlayerColor::CANNOT_DETERMINE
		&& secondOwner != PlayerColor::CANNOT_DETERMINE
		&& firstOwner != secondOwner;
}

bool controlledBySamePlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second)
{
	const auto firstOwner = battle.battleGetOwner(first);
	const auto secondOwner = battle.battleGetOwner(second);
	return firstOwner != PlayerColor::CANNOT_DETERMINE && firstOwner == secondOwner;
}

bool qualifiesForEvasiveShroud(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!battle.battleIsShroudFlankingAttack(attack))
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return newHorizonsShroud::hasEvasiveShroud(battle.battleGetFightingHero(side));
}

bool qualifiesForAmbusher(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!battle.battleIsShroudFlankingAttack(attack))
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return newHorizonsShroud::ambusherDamagePercent(
		battle.battleGetFightingHero(side), attack.attacker) > 0;
}

std::optional<BattleSide> qualifyingShadowAssaultSide(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!battle.battleIsShroudFlankingAttack(attack))
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	if(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		battle.battleGetFightingHero(side), attack.defender, side) <= 0)
		return {};
	return side;
}

void refreshEvasiveShroud(HypotheticBattle & battle, uint32_t attackerUnitId)
{
	auto attacker = battle.getForUpdate(attackerUnitId);
	if(!attacker)
		return;
	attacker->removeUnitBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
	attacker->addUnitBonus({newHorizonsShroud::evasiveShroudProtection()});
}

void spendAmbusher(HypotheticBattle & battle, uint32_t attackerUnitId)
{
	if(auto attacker = battle.getForUpdate(attackerUnitId))
		attacker->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
}

void spendShadowAssault(HypotheticBattle & battle, uint32_t targetUnitId, BattleSide attackingSide)
{
	if(auto target = battle.getForUpdate(targetUnitId))
		target->addUnitBonus({newHorizonsShroud::shadowAssaultSpentMarker(attackingSide)});
}
}

AttackerValue::AttackerValue()
	: value(0),
	isRetaliated(false)
{
}

void BattleExchangeVariant::accountForNewElementalRebirthSpawns(
	const std::set<uint32_t> & idsBefore, const battle::Unit * referenceActor,
	const bool referenceActorIsOurs, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb)
{
	if(!referenceActor)
		return;

	for(const auto unitId : hb->getElementalRebirthSpawnUnitIds())
	{
		if(idsBefore.contains(unitId) || !scoredElementalRebirthSpawnUnitIds.insert(unitId).second)
			continue;

		const auto * spawned = hb->battleGetUnitByID(unitId);
		if(!spawned || !spawned->alive() || !spawned->unitType() || spawned->getAvailableHealth() <= 0)
			continue;

		const auto enemies = hb->battleGetUnitsIf([&](const battle::Unit * candidate)
		{
			return candidate && candidate->alive() && candidate->unitType()
				&& candidate->getPosition().isValid()
				&& !hb->battleMatchOwner(spawned, candidate);
		});
		if(enemies.empty())
			continue;

		const auto fullHealth = static_cast<int64_t>(spawned->getCount()) * spawned->getMaxHealth();
		if(fullHealth <= 0)
			continue;

		// Keep this in the exchange's damage-reduction/DPS units: scale one ordinary
		// projected attack by the exact surviving HP fraction. This is a bounded
		// representative-target approximation, not a full initiative-branch forecast.
		const auto projectedDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(spawned, enemies.front(), hb));
		const float spawnValue = static_cast<float>(projectedDamage)
			* static_cast<float>(spawned->getAvailableHealth()) / static_cast<float>(fullHealth);
		const bool sameSideAsReference = spawned->unitSide() == referenceActor->unitSide();
		const bool spawnIsOurs = referenceActorIsOurs ? sameSideAsReference : !sameSideAsReference;
		if(spawnIsOurs)
			dpsScore.enemyDamageReduce += spawnValue;
		else
			dpsScore.ourDamageReduce += spawnValue;
	}
}

void BattleExchangeVariant::accountForNewPrimalBurstHits(const size_t firstNewHit,
	const PlayerColor referenceController, const bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto & hits = hb->getProjectedPrimalBurstHits();
	for(size_t index = firstNewHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		if(!hit.preHitTarget || hit.actualDamage <= 0)
			continue;
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.actualDamage), damageCache, hb);
		const bool targetSharesReferenceController = hit.targetController == referenceController;
		const bool targetIsOurs = targetSharesReferenceController == referenceActorIsOurs;
		if(targetIsOurs)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	}
}

MoveTarget::MoveTarget()
	: positions(), cachedAttack(), score(EvaluationResult::INEFFECTIVE_SCORE)
{
	turnsToReach = 1;
}

float BattleExchangeVariant::trackAttack(
	const AttackPossibility & ap,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache)
{
	if(!ap.attackerState)
	{
		logAi->trace("Skipping fake ap attack");
		return 0;
	}
	const auto rebirthSpawnIdsBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto primalBurstFirstNewHit = hb->getProjectedPrimalBurstHits().size();

	auto attacker = hb->getForUpdate(ap.attack.attacker->unitId());
	if(!attacker || attacker->armorerLastStandEndedActivation)
		return 0;
	const auto referenceController = hb->battleGetOwner(attacker.get());
	const auto originalPosition = attacker->getPosition();
	const auto attackerSide = hb->playerToSide(hb->battleGetOwner(attacker.get()));
	const auto * attackerHero = attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER
		? hb->battleGetFightingHero(attackerSide) : nullptr;
	const bool canCheckNightProwlerPath = ap.from.isValid()
		&& ap.from != originalPosition
		&& !attacker->hasBonusOfType(BonusType::FLYING)
		&& attackerHero && newHorizonsShroud::rank(attackerHero) > 0
		&& newHorizonsShroud::hasNightProwler(attackerHero);
	BattleHexArray nightProwlerPath;
	if(canCheckNightProwlerPath)
		nightProwlerPath = hb->getPath(originalPosition, ap.from, attacker.get()).first;
	const bool crossesNightProwlerEnemy = !nightProwlerPath.empty()
		&& hb->battleNightProwlerCrossesEnemy(attacker.get(), nightProwlerPath);
	if(crossesNightProwlerEnemy)
		attacker->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
	if(!ap.attack.shooting && ap.from.isValid())
		attacker->setPosition(ap.from);

	float attackValue = ap.attackValue();
	auto affectedUnits = ap.affectedUnits;

	dpsScore.ourDamageReduce += ap.attackerDamageReduce + ap.collateralDamageReduce;
	dpsScore.enemyDamageReduce += ap.defenderDamageReduce + ap.shootersBlockedDmg;
	attackerValue[attacker->unitId()].value = attackValue;

	affectedUnits.push_back(ap.attackerState);
	std::map<uint32_t, int64_t> finalDamage;
	for(const auto & affectedUnit : affectedUnits)
	{
		auto unitToUpdate = hb->getForUpdate(affectedUnit->unitId());
		finalDamage[unitToUpdate->unitId()] = std::max<int64_t>(0,
			unitToUpdate->getAvailableHealth() - affectedUnit->getAvailableHealth());
	}

	// Reapply the selected preview strike-by-strike.  This is intentionally
	// separate from the final-state snapshots: Fortune aftermath belongs
	// between strikes (and between retaliation and the triggering strike).
	if(!ap.fortuneStrikes.empty())
	{
		int64_t projectedAttackerDamage = 0;
		const auto attackerDamage = finalDamage[attacker->unitId()];
		if(ap.preAttackDamage > 0)
		{
			const bool wasAlive = attacker->alive();
			const auto rebirthSource = wasAlive ? hb->captureElementalRebirthSource(*attacker) : std::nullopt;
			const bool nativeRebirth = hb->hasReadyNativeRebirth(attacker.get());
			auto preAttackDamage = ap.preAttackDamage;
			attacker->damage(preAttackDamage);
			hb->recordBloodrageTransition(attacker, wasAlive);
			if(rebirthSource)
				hb->projectElementalRebirth(attacker.get(), *rebirthSource,
					wasAlive && !attacker->alive(), attacker->isClone(), nativeRebirth);
		}

		for(const auto & strike : ap.fortuneStrikes)
		{
			auto projectedAttacker = hb->getForUpdate(strike.attackerId);
			auto projectedDefender = hb->getForUpdate(strike.defenderId);
			BattleAttackInfo projectedAttack(projectedAttacker.get(), projectedDefender.get(), 0, strike.shooting);
			// The projection records the resolved physical-creature provenance;
			// preserve that explicit context for attack-classified post-hit effects.
			projectedAttack.physicalDamage = strike.damageProvenance
				== battle::DamageProvenance::PHYSICAL_CREATURE;
			projectedAttack.retaliation = strike.retaliation;
			projectedAttack.cleaveDamagePercent = strike.cleaveDamagePercent;
			projectedAttack.attackerPos = projectedAttacker->getPosition();
			projectedAttack.defenderPos = projectedDefender->getPosition();
			const bool hasPrimaryHit = std::ranges::any_of(strike.hits, [&strike](const auto & hit)
			{
				return hit.first == strike.defenderId;
			});
			const bool shroudFlankingAttack = hasPrimaryHit
				&& hb->battleIsShroudFlankingAttack(projectedAttack);
			const auto attackingSide = shroudFlankingAttack
				? hb->playerToSide(hb->battleGetOwner(projectedAttacker.get())) : BattleSide::NONE;
			const auto * attackingHero = attackingSide == BattleSide::ATTACKER || attackingSide == BattleSide::DEFENDER
				? hb->battleGetFightingHero(attackingSide) : nullptr;
			const bool triggersAmbusher = shroudFlankingAttack
				&& newHorizonsShroud::ambusherDamagePercent(attackingHero, projectedAttacker.get()) > 0;
			const auto shadowAssaultSide = hasPrimaryHit
				? qualifyingShadowAssaultSide(*hb, projectedAttack) : std::optional<BattleSide>{};
			const bool triggersEvasiveShroud = shroudFlankingAttack
				&& newHorizonsShroud::hasEvasiveShroud(attackingHero);
			if(strike.protectIntercepted)
				hb->consumeHeroOrderProtectInterception(ap.attack.defender->unitId(), strike.defenderId);
			std::vector<std::pair<uint32_t, int64_t>> actualHits;
			actualHits.reserve(strike.hits.size());
			struct PendingRebirth
			{
				newHorizonsElementalRebirth::DeathSnapshot snapshot;
				bool cloneKilled = false;
				bool nativeRebirth = false;
			};
			std::map<uint32_t, PendingRebirth> pendingRebirths;
			bool enemyStackKilled = false;
			bool endedActiveActivationThisStrike = false;
			for(const auto & [unitId, damage] : strike.hits)
			{
				auto target = hb->getForUpdate(unitId);
				auto appliedDamage = std::max<int64_t>(0, damage);
				const auto standSide = hb->playerToSide(hb->battleGetOwner(target.get()));
				const bool validStandSide = standSide == BattleSide::ATTACKER || standSide == BattleSide::DEFENDER;
				const bool eligibleForLastStand = strike.eligibleForLastStand && validStandSide
					&& newHorizonsArmorer::canTriggerLastStand(hb->battleGetOwnerHero(target.get()), target.get());
				const auto lastStand = validStandSide
					? newHorizonsArmorer::resolveLastStandDamage(appliedDamage,
						target->getGuardianSpiritHitPoints(), target->getGuardianSpiritRoundsRemaining(),
						target->getAvailableHealth(), eligibleForLastStand,
						hb->armorerLastStandUsed(standSide))
					: newHorizonsArmorer::ArmorerLastStandDamageResult{appliedDamage, false};
				appliedDamage = lastStand.damageToApply;
				if(lastStand.triggered)
					hb->consumeArmorerLastStand(standSide);
				const bool consumesBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
					projectedAttacker.get(), strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE)
					&& hb->battleHasBastionProtection(target.get());
				const auto wasAlive = target->alive();
				if(wasAlive && !pendingRebirths.contains(unitId))
				{
					if(const auto snapshot = hb->captureElementalRebirthSource(*target))
						pendingRebirths.emplace(unitId, PendingRebirth{
							*snapshot, target->isClone(), hb->hasReadyNativeRebirth(target.get())});
				}
				const auto healthBefore = target->getAvailableHealth();
				const auto projectedDamage = battleAIProjectDamage(target.get(), appliedDamage,
					strike.damageProvenance);
				target->damage(appliedDamage, false, strike.damageProvenance);
				if(lastStand.triggered)
				{
					hb->applyArmorerLastStandDefend(unitId);
					const auto activeStackId = hb->getActiveStackID();
					if(strike.retaliation && activeStackId >= 0
						&& unitId == static_cast<uint32_t>(activeStackId))
					{
						target->armorerLastStandEndedActivation = true;
						endedActiveActivationThisStrike = true;
					}
				}
				if(consumesBastion)
					target->armorerBastionRound = hb->battleGetRound();
				const auto healthLoss = std::max<int64_t>(0,
					healthBefore - target->getAvailableHealth());
				if(unitId == attacker->unitId())
					projectedAttackerDamage += healthLoss;
				actualHits.emplace_back(unitId, appliedDamage);
				if(projectedDamage.healthLoss > 0)
				{
					for(const auto & [noQuarterTargetId, moraleActivations] : strike.noQuarterTargets)
						if(noQuarterTargetId == unitId)
							target->applyNoQuarter(moraleActivations, true);
					hb->recordBloodrageTransition(target, wasAlive);
					const bool luckAffectedTarget = unitId == strike.defenderId
						|| hb->getLuckRollRules().affectsAllTargets;
					if(wasAlive && !target->alive() && luckAffectedTarget
							&& hb->battleMatchOwner(projectedAttacker.get(), target.get()))
						enemyStackKilled = true;
				}
			}
			// Each projected striker spends its one-attack reward after its full hit set.
			// The movement bonus belongs to the moved stack, so an enemy preemptive
			// strike cannot consume it.
			if(!strike.hits.empty() && hasNightProwlerBonus(projectedAttacker.get()))
				projectedAttacker->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));

			// Spend Ambusher on the accepted direct hit even if its attacker did not
			// survive; Evasive Shroud refreshes only for a surviving attacker.
			if(triggersAmbusher)
				projectedAttacker->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
			if(shadowAssaultSide)
				spendShadowAssault(*hb, projectedDefender->unitId(), *shadowAssaultSide);
			if(triggersEvasiveShroud && projectedAttacker->alive())
			{
				projectedAttacker->removeUnitBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
				projectedAttacker->addUnitBonus({newHorizonsShroud::evasiveShroudProtection()});
			}
			if(strike.cleaveDamagePercent > 0)
				projectedAttacker->cleaveUsedThisActivation = true;
			if(strike.perfectMoment && !strike.retaliation)
			{
				const auto side = hb->playerToSide(hb->battleGetOwner(projectedAttacker.get()));
				auto fortune = hb->getSylvanLuckState(side);
				projectedAttack.luckyStrike = fortune.consumePerfectMoment();
				hb->setSylvanLuckState(side, fortune);
			}
			hb->projectFortuneStrike(projectedAttack, actualHits, projectedAttacker.get(), enemyStackKilled,
				strike.resolvedLuck, true, strike.perfectFortune, strike.perfectFortuneSide);
			hb->projectRangedMarkStrike(projectedAttack, actualHits);
			for(const auto & [unitId, pending] : pendingRebirths)
			{
				auto target = hb->getForUpdate(unitId);
				hb->projectElementalRebirth(target.get(), pending.snapshot,
					!target->alive(), pending.cloneKilled, pending.nativeRebirth);
			}
			const auto healthBeforeHexOfPain = projectedAttacker->getAvailableHealth();
			hb->projectHexOfPainStrike(projectedAttack, actualHits, strike.attackIndex);
			if(projectedAttacker->unitId() == attacker->unitId())
				projectedAttackerDamage += std::max<int64_t>(0,
					healthBeforeHexOfPain - projectedAttacker->getAvailableHealth());
			// Protect applies to the captured primary target of each blow, not
			// once to the whole multi-attack action. AttackPossibility snapshots
			// the redirected ID before damage, so a killed Protector still records
			// the actual first target; later strikes can correctly target the Ward.
			if(strike.relentlessAssaultEligible)
			{
				auto projectedHeroOrderAttack = ap.attack;
				projectedHeroOrderAttack.protectIntercepted = strike.protectIntercepted;
				const auto primaryTarget = strike.relentlessAssaultEligible
					? std::optional<uint32_t>(strike.defenderId) : std::nullopt;
				projectRelentlessAssaultAttack(*hb, projectedHeroOrderAttack, primaryTarget);
			}
			if(endedActiveActivationThisStrike)
				break;
		}

		// A preview can contain damage sources which are not represented by a
		// physical strike (for example, a reaction). Apply that residual only
		// after replaying the known strikes so it cannot move a defender's
		// Fortune activation boundary or kill a retaliation attacker early.
		if(attackerDamage > projectedAttackerDamage + ap.preAttackDamage)
		{
			const bool wasAlive = attacker->alive();
			const auto rebirthSource = wasAlive ? hb->captureElementalRebirthSource(*attacker) : std::nullopt;
			const bool nativeRebirth = hb->hasReadyNativeRebirth(attacker.get());
			auto residual = attackerDamage - projectedAttackerDamage - ap.preAttackDamage;
			attacker->damage(residual);
			hb->recordBloodrageTransition(attacker, wasAlive);
			if(rebirthSource)
				hb->projectElementalRebirth(attacker.get(), *rebirthSource,
					wasAlive && !attacker->alive(), attacker->isClone(), nativeRebirth);
		}
	}
	else
	{
		// Older/partial previews may not carry strike metadata. Preserve their
		// health projection, but do not invent Fortune transitions for them.
		for(const auto & affectedUnit : affectedUnits)
		{
			auto unitToUpdate = hb->getForUpdate(affectedUnit->unitId());
			auto damageDealt = finalDamage[unitToUpdate->unitId()];
			if(damageDealt > 0)
			{
				const bool wasAlive = unitToUpdate->alive();
				const auto rebirthSource = wasAlive
					? hb->captureElementalRebirthSource(*unitToUpdate) : std::nullopt;
				const bool nativeRebirth = hb->hasReadyNativeRebirth(unitToUpdate.get());
				unitToUpdate->damage(damageDealt);
				hb->recordBloodrageTransition(unitToUpdate, wasAlive);
				if(rebirthSource)
					hb->projectElementalRebirth(unitToUpdate.get(), *rebirthSource,
						wasAlive && !unitToUpdate->alive(), unitToUpdate->isClone(), nativeRebirth);
			}
		}
	}

	if(hb->getActiveStackID() == static_cast<int32_t>(attacker->unitId()))
		attacker->consumeNoQuarterActivation();
	if(ap.bulwarkMireGripTriggered && attacker->alive())
	{
		attacker->bulwarkMireGripApplied = true;
		const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		const Bonus slow(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, -2, BonusSourceID(SecondarySkill(bulwarkSkillId)));
		attacker->addUnitBonus({slow});
	}

	for(auto affectedUnit : affectedUnits)
	{
		auto unitToUpdate = hb->getForUpdate(affectedUnit->unitId());
		// The preview may contain several strikes or consume only one of several
		// retaliations. Copy consumption, not a boolean can-retaliate transition.
		// CAmmo assignment keeps this unit's owner-bound bonus caches intact.
		static_cast<battle::CAmmo &>(unitToUpdate->shots) = affectedUnit->shots;
		static_cast<battle::CAmmo &>(unitToUpdate->counterAttacks) = affectedUnit->counterAttacks;
		unitToUpdate->battlecraftWaitBonusUsed = affectedUnit->battlecraftWaitBonusUsed;
		unitToUpdate->battlecraftWaitMasteryDoubled = affectedUnit->battlecraftWaitMasteryDoubled;
		unitToUpdate->battlecraftDefendMasteryDoubled = affectedUnit->battlecraftDefendMasteryDoubled;
		unitToUpdate->battlecraftPreemptiveStrikeRound = affectedUnit->battlecraftPreemptiveStrikeRound;
		unitToUpdate->cleaveUsedThisActivation = affectedUnit->cleaveUsedThisActivation;
		unitToUpdate->bulwarkPreemptiveUsed = affectedUnit->bulwarkPreemptiveUsed;
		unitToUpdate->bulwarkMireGripApplied = affectedUnit->bulwarkMireGripApplied;
		unitToUpdate->bulwarkDefendPhysicalDamage = affectedUnit->bulwarkDefendPhysicalDamage;
		unitToUpdate->bulwarkImmovableRound = affectedUnit->bulwarkImmovableRound;
		unitToUpdate->armorerBastionRound = affectedUnit->armorerBastionRound;
		unitToUpdate->armorerLastStandEndedActivation = affectedUnit->armorerLastStandEndedActivation;
		unitToUpdate->armorerLastStandDefending = affectedUnit->armorerLastStandDefending;
		unitToUpdate->bulwarkToxicSpinesRound = affectedUnit->bulwarkToxicSpinesRound;
		unitToUpdate->physicalPoisonBaseDamage = affectedUnit->physicalPoisonBaseDamage;
		unitToUpdate->physicalPoisonActivationsRemaining = affectedUnit->physicalPoisonActivationsRemaining;
		unitToUpdate->physicalPoisonSourceStackId = affectedUnit->physicalPoisonSourceStackId;
		unitToUpdate->guardianSpiritHitPoints = affectedUnit->guardianSpiritHitPoints;
		unitToUpdate->guardianSpiritRoundsRemaining = affectedUnit->guardianSpiritRoundsRemaining;
		unitToUpdate->rangedFollowUpDamagePercent = affectedUnit->rangedFollowUpDamagePercent;
		// The forecasted continuation is one accepted shot. Consume its copied
		// allowance here so this detached branch cannot price another reduced
		// shot on the same activation or carry it into the next one.
		if(unitToUpdate->unitId() == attacker->unitId()
			&& ap.attack.shooting && ap.attack.physicalDamage
			&& unitToUpdate->rangedFollowUpDamagePercent > 0)
			unitToUpdate->rangedFollowUpDamagePercent = 0;
		// The detached forecast records explicitly classified hits and reactions
		// even when they are not represented by a Fortune strike. Copy its total
		// interval rather than guessing that residual HP loss was physical.
		unitToUpdate->veteranPhysicalDamageSinceActivation =
			affectedUnit->veteranPhysicalDamageSinceActivation;

		if(unitToUpdate->unitSide() == attacker->unitSide())
		{
			if(unitToUpdate->unitId() == attacker->unitId())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s -> %s, ap retaliation, %s, dps: %lld",
					hb->getForUpdate(ap.attack.defender->unitId())->getDescription(),
					ap.attack.attacker->getDescription(),
					ap.attack.shooting ? "shot" : "mellee",
					damageDealt);
#endif
			}
			else
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s, ap collateral, dps: %lld",
					unitToUpdate->getDescription(),
					damageDealt);
#endif
			}
		}
		else
		{
			if(unitToUpdate->unitId() == ap.attack.defender->unitId())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s -> %s, ap attack, %s, dps: %lld",
					attacker->getDescription(),
					ap.attack.defender->getDescription(),
					ap.attack.shooting ? "shot" : "mellee",
					damageDealt);
#endif
			}
			else
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s, ap enemy collateral, dps: %lld",
					unitToUpdate->getDescription(),
					damageDealt);
#endif
			}
		}
	}
	if(!ap.attack.shooting && ap.from.isValid() && attacker->alive()
		&& attacker->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		&& !attacker->hasBonusOfType(BonusType::NOT_ACTIVE)
		&& !attacker->hasBonusOfType(BonusType::BIND_EFFECT))
		attacker->setPosition(originalPosition);

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace(
		"ap score: our: %2f, enemy: %2f, collateral: %2f, blocked: %2f",
		ap.attackerDamageReduce,
		ap.defenderDamageReduce,
		ap.collateralDamageReduce,
		ap.shootersBlockedDmg);
#endif
	accountForNewElementalRebirthSpawns(rebirthSpawnIdsBefore, attacker.get(), true, damageCache, hb);
	accountForNewPrimalBurstHits(primalBurstFirstNewHit, referenceController, true, damageCache, hb);

	return attackValue;
}

float BattleExchangeVariant::trackAttack(
	std::shared_ptr<StackWithBonuses> attacker,
	std::shared_ptr<StackWithBonuses> defender,
	bool shooting,
	bool isOurAttack,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	bool evaluateOnly,
	bool allowRetaliation)
{
	if(!attacker || attacker->armorerLastStandEndedActivation)
		return 0;

	const auto rebirthSpawnIdsBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto primalBurstFirstNewHit = hb->getProjectedPrimalBurstHits().size();
	const auto referenceController = hb->battleGetOwner(attacker.get());
	const std::string cachingStringBlocksRetaliation = "type_BLOCKS_RETALIATION";
	static const auto selectorBlocksRetaliation = Selector::type()(BonusType::BLOCKS_RETALIATION);
	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	const bool counterAttacksBlocked = attacker->hasBonus(selectorBlocksRetaliation, cachingStringBlocksRetaliation);
	const auto requestedDefender = defender;
	BattleAttackInfo requestedAttack(attacker.get(), requestedDefender.get(), 0, shooting);
	const auto relentlessAssaultTargetUnitId = relentlessAssaultPrimaryTargetId(*hb, requestedAttack);
	const auto * redirectedDefender = hb->battleResolveHeroOrderTarget(attacker.get(), requestedDefender.get(), shooting);
	const bool protectIntercepted = !shooting && redirectedDefender
		&& redirectedDefender->unitId() != requestedDefender->unitId();
	if(protectIntercepted && !evaluateOnly)
		hb->consumeHeroOrderProtectInterception(requestedDefender->unitId(), redirectedDefender->unitId());
	requestedAttack.protectIntercepted = protectIntercepted;
	if(protectIntercepted)
	{
		defender = hb->getForUpdate(redirectedDefender->unitId());
		if(!defender)
			return 0;
	}
	BattleAttackInfo projectedAttack(attacker.get(), defender.get(), 0, shooting);
	projectedAttack.protectIntercepted = protectIntercepted;
	const bool perfectFortune = hb->battleCanTriggerPerfectFortune(projectedAttack);
	const auto perfectFortuneSide = hb->playerToSide(hb->battleGetActionController(projectedAttack.attacker));
	const auto resolvedLuck = hb->captureFortuneStrikeOutcome(projectedAttack);
	const bool triggersEvasiveShroud = !evaluateOnly
		&& qualifiesForEvasiveShroud(*hb, projectedAttack);
	const bool triggersAmbusher = !evaluateOnly
		&& qualifiesForAmbusher(*hb, projectedAttack);
	const auto shadowAssaultSide = !evaluateOnly
		? qualifyingShadowAssaultSide(*hb, projectedAttack) : std::optional<BattleSide>{};
	const bool nightProwlerPending = hasNightProwlerBonus(attacker.get());
	const auto resolveLastStand = [&hb](const BattleAttackInfo & attack,
		const battle::Unit * target, int64_t incomingDamage)
	{
		const auto side = target ? hb->playerToSide(hb->battleGetOwner(target)) : BattleSide::NONE;
		const bool validSide = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
		if(!validSide)
			return std::pair{newHorizonsArmorer::ArmorerLastStandDamageResult{incomingDamage, false}, side};

		const bool eligible = newHorizonsArmorer::isEligiblePhysicalAttack(attack.attacker,
			attack.physicalDamage, attack.shooting && attack.attacker
				&& attack.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
			&& newHorizonsArmorer::canTriggerLastStand(hb->battleGetOwnerHero(target), target);
		const auto result = newHorizonsArmorer::resolveLastStandDamage(incomingDamage,
			target->getGuardianSpiritHitPoints(), target->getGuardianSpiritRoundsRemaining(),
			target->getAvailableHealth(), eligible, hb->armorerLastStandUsed(side));
		return std::pair{result, side};
	};
	const auto applyLastStandDefend = [&hb, evaluateOnly](StackWithBonuses * target,
		const newHorizonsArmorer::ArmorerLastStandDamageResult & lastStand,
		BattleSide side, bool retaliation)
	{
		if(evaluateOnly || !lastStand.triggered || !target)
			return;
		hb->consumeArmorerLastStand(side);
		hb->applyArmorerLastStandDefend(target->unitId());
		const auto activeStackId = hb->getActiveStackID();
		if(retaliation && activeStackId >= 0
			&& target->unitId() == static_cast<uint32_t>(activeStackId))
			target->armorerLastStandEndedActivation = true;
	};

	int64_t attackDamage = damageCache.getDamage(attacker.get(), defender.get(), hb);
	const auto attackLastStand = resolveLastStand(projectedAttack, defender.get(), attackDamage);
	attackDamage = attackLastStand.first.damageToApply;
	const auto attackDamageProvenance = battleAIDamageProvenance(
		attacker.get(), projectedAttack.physicalDamage);
	const bool consumesBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
		projectedAttack.attacker, projectedAttack.physicalDamage)
		&& hb->battleHasBastionProtection(defender.get());
	const auto projectedAttackDamage = battleAIProjectDamage(
		defender.get(), attackDamage, attackDamageProvenance);
	const int64_t actualDamage = projectedAttackDamage.appliedDamage;
	float defenderDamageReduce = AttackPossibility::calculateDamageReduce(attacker.get(), defender.get(),
		projectedAttackDamage.healthLoss, damageCache, hb);
	float attackerDamageReduce = 0;
	const bool defenderWasAlive = defender->alive();
	const int64_t defenderHealthBeforeAttack = defender->getAvailableHealth();
	const auto defenderRebirthSource = defenderWasAlive
		? hb->captureElementalRebirthSource(*defender) : std::nullopt;
	const bool defenderMayRebirth = hb->hasReadyNativeRebirth(defender.get());
	auto projectedAttacker = evaluateOnly ? attacker->acquireState()
		: std::static_pointer_cast<battle::CUnitState>(attacker);

	if(!evaluateOnly)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace(
			"%s -> %s, normal attack, %s, dps: %lld, %2f",
			attacker->getDescription(),
			defender->getDescription(),
			shooting ? "shot" : "mellee",
			attackDamage,
			defenderDamageReduce);
#endif

		if(isOurAttack)
		{
			dpsScore.enemyDamageReduce += defenderDamageReduce;
			attackerValue[attacker->unitId()].value += defenderDamageReduce;
		}
		else
			dpsScore.ourDamageReduce += defenderDamageReduce;

		defender->damage(attackDamage, false, attackDamageProvenance);
		applyLastStandDefend(defender.get(), attackLastStand.first, attackLastStand.second,
			projectedAttack.retaliation);
		if(nightProwlerPending)
			attacker->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
		if(triggersAmbusher)
			spendAmbusher(*hb, attacker->unitId());
		if(shadowAssaultSide)
			spendShadowAssault(*hb, defender->unitId(), *shadowAssaultSide);
		if(triggersEvasiveShroud && attacker->alive())
			refreshEvasiveShroud(*hb, attacker->unitId());
		if(consumesBastion)
			defender->armorerBastionRound = hb->battleGetRound();
		if(actualDamage > 0)
			projectNoQuarterAfterHit(*hb, projectedAttack, *defender);
		hb->recordBloodrageTransition(defender, defenderWasAlive);
		hb->projectFortuneStrike(projectedAttack, {{defender->unitId(), actualDamage}}, attacker.get(),
			defenderWasAlive && !defender->alive() && hb->battleMatchOwner(attacker.get(), defender.get()),
			resolvedLuck, true, perfectFortune, perfectFortuneSide);
		hb->projectRangedMarkStrike(projectedAttack, {{defender->unitId(), actualDamage}});
		if(defenderRebirthSource)
			hb->projectElementalRebirth(defender.get(), *defenderRebirthSource,
				defenderWasAlive && !defender->alive(), defender->isClone(), defenderMayRebirth);
		if(relentlessAssaultTargetUnitId)
			projectRelentlessAssaultAttack(*hb, requestedAttack, relentlessAssaultTargetUnitId);
	}
	projectedAttacker->afterAttack(shooting, false, projectedAttack.physicalDamage);
	if(!evaluateOnly && shooting && projectedAttack.physicalDamage
		&& attacker->rangedFollowUpDamagePercent > 0)
		attacker->rangedFollowUpDamagePercent = 0;
	const auto projectHexOfPain = [&](const battle::Unit * actor, const battle::Unit * target,
		bool isShooting, bool isCounter, const std::vector<std::pair<uint32_t, int64_t>> & hits)
	{
		if(!actor || !newHorizonsHexOfPainAI::hasEffect(actor) || hits.empty())
			return int64_t{0};

		auto eventBattle = evaluateOnly
			? std::make_shared<HypotheticBattle>(hb->env, hb)
			: hb;
		auto eventActor = eventBattle->getForUpdate(actor->unitId());
		auto eventTarget = target ? eventBattle->getForUpdate(target->unitId()) : nullptr;
		BattleAttackInfo eventAttack(eventActor.get(), eventTarget.get(), 0, isShooting);
		eventAttack.retaliation = isCounter;
		return eventBattle->projectHexOfPainStrike(eventAttack, hits);
	};
	const auto recordHexOfPain = [&](const battle::Unit * actorBeforeTrigger, int64_t damage)
	{
		if(!actorBeforeTrigger || damage <= 0)
			return;

		const auto value = AttackPossibility::calculateDamageReduce(nullptr, actorBeforeTrigger,
			static_cast<uint64_t>(damage), damageCache, hb);
		const bool sameSideAsMainAttacker = hb->battleMatchOwner(attacker.get(), actorBeforeTrigger, true);
		if(sameSideAsMainAttacker)
			attackerDamageReduce += value;
		else
			defenderDamageReduce += value;

		const bool actorOnOurSide = sameSideAsMainAttacker == isOurAttack;
		if(actorOnOurSide)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
		attackerValue[actorBeforeTrigger->unitId()].value -= value;
	};
	const auto mainHexScoringActor = projectedAttacker->acquireState();
	const auto mainHexDamage = projectHexOfPain(projectedAttacker.get(), defender.get(), shooting, false,
		{{defender->unitId(), actualDamage}});
	recordHexOfPain(mainHexScoringActor.get(), mainHexDamage);

	const bool projectedEnemyKill = defenderWasAlive && !defenderMayRebirth
		&& (evaluateOnly
			? projectedAttackDamage.healthLoss >= defenderHealthBeforeAttack
			: !defender->alive());
	if(!shooting && projectedAttacker->alive() && projectedEnemyKill
		&& hb->battleMatchOwner(attacker.get(), defender.get())
		&& hb->battleCanTriggerCleave(projectedAttacker.get()))
	{
		if(const auto * selected = hb->battleSelectCleaveTarget(projectedAttacker.get(), defender.get()))
		{
			auto target = evaluateOnly
				? std::shared_ptr<StackWithBonuses>{}
				: hb->getForUpdate(selected->unitId());
			const battle::Unit * targetUnit = target ? target.get() : selected;
			BattleAttackInfo cleaveAttack(projectedAttacker.get(), targetUnit, 0, false);
			cleaveAttack.attackerPos = projectedAttacker->getPosition();
			cleaveAttack.defenderPos = targetUnit->getPosition();
			cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
			const bool triggersCleaveEvasiveShroud = !evaluateOnly
				&& qualifiesForEvasiveShroud(*hb, cleaveAttack);
			const bool triggersCleaveAmbusher = !evaluateOnly
				&& qualifiesForAmbusher(*hb, cleaveAttack);
			const auto cleaveShadowAssaultSide = !evaluateOnly
				? qualifyingShadowAssaultSide(*hb, cleaveAttack) : std::optional<BattleSide>{};
			const auto cleaveProvenance = battleAIDamageProvenance(
				projectedAttacker.get(), cleaveAttack.physicalDamage);
			const bool consumesCleaveBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
				cleaveAttack.attacker, cleaveAttack.physicalDamage)
				&& hb->battleHasBastionProtection(targetUnit);
			projectedAttacker->cleaveUsedThisActivation = true;

			const bool cleavePerfectFortune = hb->battleCanTriggerPerfectFortune(cleaveAttack);
			const auto cleavePerfectFortuneSide = hb->playerToSide(hb->battleGetActionController(cleaveAttack.attacker));
			const auto cleaveResolvedLuck = hb->captureFortuneStrikeOutcome(cleaveAttack);
			int64_t cleaveDamage = hb->battleExpectedLuckDamage(cleaveAttack);
			const auto cleaveLastStand = resolveLastStand(cleaveAttack, targetUnit, cleaveDamage);
			cleaveDamage = cleaveLastStand.first.damageToApply;
			const auto projectedCleaveDamage = battleAIProjectDamage(
				targetUnit, cleaveDamage, cleaveProvenance);
			const float cleaveDamageReduce = AttackPossibility::calculateDamageReduce(
				projectedAttacker.get(), targetUnit, projectedCleaveDamage.healthLoss, damageCache, hb);
			defenderDamageReduce += cleaveDamageReduce;
			int64_t resolvedCleaveDamage = projectedCleaveDamage.appliedDamage;

			if(!evaluateOnly)
			{
				if(isOurAttack)
				{
					dpsScore.enemyDamageReduce += cleaveDamageReduce;
					attackerValue[attacker->unitId()].value += cleaveDamageReduce;
				}
				else
					dpsScore.ourDamageReduce += cleaveDamageReduce;

				const bool targetWasAlive = target->alive();
				const auto targetRebirthSource = targetWasAlive
					? hb->captureElementalRebirthSource(*target) : std::nullopt;
				const bool targetMayRebirth = hb->hasReadyNativeRebirth(target.get());
				target->damage(cleaveDamage, false, cleaveProvenance);
				applyLastStandDefend(target.get(), cleaveLastStand.first,
					cleaveLastStand.second, cleaveAttack.retaliation);
				if(triggersCleaveAmbusher)
					spendAmbusher(*hb, projectedAttacker->unitId());
				if(cleaveShadowAssaultSide)
					spendShadowAssault(*hb, target->unitId(), *cleaveShadowAssaultSide);
				if(triggersCleaveEvasiveShroud && projectedAttacker->alive())
					refreshEvasiveShroud(*hb, projectedAttacker->unitId());
				if(consumesCleaveBastion)
					target->armorerBastionRound = hb->battleGetRound();
				resolvedCleaveDamage = cleaveDamage;
				if(resolvedCleaveDamage > 0)
					projectNoQuarterAfterHit(*hb, cleaveAttack, *target);
				hb->recordBloodrageTransition(target, targetWasAlive);
				hb->projectFortuneStrike(cleaveAttack, {{target->unitId(), cleaveDamage}}, attacker.get(),
					targetWasAlive && !target->alive() && hb->battleMatchOwner(attacker.get(), target.get()),
					cleaveResolvedLuck, true, cleavePerfectFortune, cleavePerfectFortuneSide);
				hb->projectRangedMarkStrike(cleaveAttack, {{target->unitId(), cleaveDamage}});
				if(targetRebirthSource)
					hb->projectElementalRebirth(target.get(), *targetRebirthSource,
						targetWasAlive && !target->alive(), target->isClone(), targetMayRebirth);
			}
				projectedAttacker->afterAttack(false, false, cleaveAttack.physicalDamage);
				const auto cleaveHexScoringActor = projectedAttacker->acquireState();
				const auto cleaveHexDamage = projectHexOfPain(projectedAttacker.get(), targetUnit,
					false, false, {{targetUnit->unitId(), resolvedCleaveDamage}});
				recordHexOfPain(cleaveHexScoringActor.get(), cleaveHexDamage);
			}
	}

	if(!evaluateOnly && allowRetaliation && attacker->alive() && defender->alive()
		&& defender->ableToRetaliate() && !counterAttacksBlocked && !shooting
		&& (!hb->battleShroudDeniesRetaliation(projectedAttack) || defender->hasBonus(firstStrikeSelector)))
	{
		BattleAttackInfo retaliationAttack(defender.get(), attacker.get(), 0, false);
		retaliationAttack.retaliation = true;
		const bool retaliationPerfectFortune = hb->battleCanTriggerPerfectFortune(retaliationAttack);
		const auto retaliationPerfectFortuneSide = hb->playerToSide(hb->battleGetActionController(retaliationAttack.attacker));
		const auto retaliationResolvedLuck = hb->captureFortuneStrikeOutcome(retaliationAttack);
		const bool triggersRetaliationEvasiveShroud = qualifiesForEvasiveShroud(*hb, retaliationAttack);
		const bool triggersRetaliationAmbusher = qualifiesForAmbusher(*hb, retaliationAttack);
		const auto retaliationShadowAssaultSide = qualifyingShadowAssaultSide(*hb, retaliationAttack);
		const bool retaliationNightProwlerPending = hasNightProwlerBonus(defender.get());
		auto retaliationDamage = hb->battleExpectedLuckDamage(retaliationAttack);
		const auto retaliationLastStand = resolveLastStand(
			retaliationAttack, attacker.get(), retaliationDamage);
		retaliationDamage = retaliationLastStand.first.damageToApply;
		const bool consumesRetaliationBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
			retaliationAttack.attacker, retaliationAttack.physicalDamage)
			&& hb->battleHasBastionProtection(attacker.get());
		const auto retaliationProvenance = battleAIDamageProvenance(
			defender.get(), retaliationAttack.physicalDamage);
		const auto projectedRetaliationDamage = battleAIProjectDamage(
			attacker.get(), retaliationDamage, retaliationProvenance);
		attackerDamageReduce = AttackPossibility::calculateDamageReduce(defender.get(), attacker.get(),
			projectedRetaliationDamage.healthLoss, damageCache, hb);

#if BATTLE_TRACE_LEVEL>=1
		logAi->trace(
			"%s -> %s, retaliation, dps: %lld, %2f",
			defender->getDescription(),
			attacker->getDescription(),
			retaliationDamage,
			attackerDamageReduce);
#endif

		if(isOurAttack)
		{
			dpsScore.ourDamageReduce += attackerDamageReduce;
			attackerValue[attacker->unitId()].isRetaliated = true;
		}
		else
		{
			dpsScore.enemyDamageReduce += attackerDamageReduce;
			attackerValue[defender->unitId()].value += attackerDamageReduce;
		}

		const int64_t actualDamage = projectedRetaliationDamage.appliedDamage;
		const bool attackerWasAlive = attacker->alive();
		const auto attackerRebirthSource = attackerWasAlive
			? hb->captureElementalRebirthSource(*attacker) : std::nullopt;
		const bool attackerMayRebirth = hb->hasReadyNativeRebirth(attacker.get());
		attacker->damage(retaliationDamage, false, retaliationProvenance);
		applyLastStandDefend(attacker.get(), retaliationLastStand.first,
			retaliationLastStand.second, retaliationAttack.retaliation);
		if(retaliationNightProwlerPending)
			defender->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
		if(triggersRetaliationAmbusher)
			spendAmbusher(*hb, defender->unitId());
		if(retaliationShadowAssaultSide)
			spendShadowAssault(*hb, attacker->unitId(), *retaliationShadowAssaultSide);
		if(triggersRetaliationEvasiveShroud && defender->alive())
			refreshEvasiveShroud(*hb, defender->unitId());
		if(consumesRetaliationBastion)
			attacker->armorerBastionRound = hb->battleGetRound();
		if(actualDamage > 0)
			projectNoQuarterAfterHit(*hb, retaliationAttack, *attacker);
		hb->recordBloodrageTransition(attacker, attackerWasAlive);
		hb->projectFortuneStrike(retaliationAttack, {{attacker->unitId(), actualDamage}}, defender.get(),
			attackerWasAlive && !attacker->alive() && hb->battleMatchOwner(defender.get(), attacker.get()),
			retaliationResolvedLuck, true, retaliationPerfectFortune, retaliationPerfectFortuneSide);
		if(attackerRebirthSource)
			hb->projectElementalRebirth(attacker.get(), *attackerRebirthSource,
				attackerWasAlive && !attacker->alive(), attacker->isClone(), attackerMayRebirth);
		defender->afterAttack(false, true, retaliationAttack.physicalDamage);
		const auto retaliationHexScoringActor = defender->acquireState();
		const auto retaliationHexDamage = projectHexOfPain(defender.get(), attacker.get(), false, true,
			{{attacker->unitId(), actualDamage}});
		recordHexOfPain(retaliationHexScoringActor.get(), retaliationHexDamage);
	}
	if(!evaluateOnly && hb->getActiveStackID() == static_cast<int32_t>(attacker->unitId()))
		attacker->consumeNoQuarterActivation();

	auto score = defenderDamageReduce - attackerDamageReduce;

#if BATTLE_TRACE_LEVEL>=1
	if(!score)
	{
		logAi->trace("Attack has zero score def:%2f att:%2f", defenderDamageReduce, attackerDamageReduce);
	}
#endif
	accountForNewElementalRebirthSpawns(rebirthSpawnIdsBefore, attacker.get(), isOurAttack, damageCache, hb);
	accountForNewPrimalBurstHits(primalBurstFirstNewHit, referenceController, isOurAttack, damageCache, hb);

	return score;
}

float BattleExchangeEvaluator::scoreValue(const BattleScore & score) const
{
	return score.enemyDamageReduce * getPositiveEffectMultiplier() - score.ourDamageReduce * getNegativeEffectMultiplier();
}

EvaluationResult BattleExchangeEvaluator::findBestTarget(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	bool siegeDefense)
{
	EvaluationResult result(targets.bestAction());

	if(!activeStack->acquireState()->waitedThisTurn && !activeStack->acquireState()->hadMorale)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating waited attack for %s", activeStack->getDescription());
#endif

		auto hbWaited = std::make_shared<HypotheticBattle>(env.get(), hb);

		hbWaited->makeWait(activeStack);

		updateReachabilityMap(hbWaited);

		for(auto & ap : targets.possibleAttacks)
		{
			if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
				continue;

			float score = evaluateExchange(ap, 0, targets, damageCache, hbWaited);

			if(score > result.score)
			{
				result.score = score;
				result.bestAttack = ap;
				result.wait = true;

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("New high score %2f", result.score);
#endif
			}
		}
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Evaluating normal attack for %s", activeStack->getDescription());
#endif

	updateReachabilityMap(hb);

	if(result.bestAttack.attack.shooting
		&& !result.bestAttack.defenderDead
		&& !activeStack->acquireState()->waitedThisTurn
		&& hb->battleHasShootingPenalty(activeStack, result.bestAttack.dest))
	{
		if(!canBeHitThisTurn(result.bestAttack))
			return result; // lets wait
	}

	for(auto & ap : targets.possibleAttacks)
	{
		if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
			continue;

		float score = evaluateExchange(ap, 0, targets, damageCache, hb);
		bool sameScoreButWaited = vstd::isAlmostEqual(score, result.score) && result.wait;

		if(score > result.score || sameScoreButWaited)
		{
			result.score = score;
			result.bestAttack = ap;
			result.wait = false;

#if BATTLE_TRACE_LEVEL >= 1
			logAi->trace("New high score %2f", result.score);
#endif
		}
	}

	return result;
}

ReachabilityInfo getReachabilityWithEnemyBypass(
	const battle::Unit * activeStack,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> state)
{
	ReachabilityInfo::Parameters params(activeStack, activeStack->getPosition());
	// This path constructs Parameters directly (to account for destructible
	// enemy stacks), so preserve the same Shroud rank gate as the normal
	// callback-created reachability cache. Resolve the current controller through
	// the visibility-aware fighting-hero query rather than reconstructing a hidden
	// opponent hero from its battle side.
	const auto controllerSide = state->playerToSide(state->battleGetOwner(activeStack));
	if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		params.ghostWalk = newHorizonsShroud::rank(state->battleGetFightingHero(controllerSide)) > 0;
	state->configurePassingLines(activeStack, params);

	if(!params.flying && !params.ghostWalk)
	{
		for(const auto * unit : state->battleAliveUnits())
		{
			if(!controlledByDifferentPlayer(*state, activeStack, unit))
				continue;

			auto dmg = damageCache.getOriginalDamage(activeStack, unit, state);
			auto turnsToKill = unit->getAvailableHealth() / std::max(dmg, (int64_t)1);

			vstd::amin(turnsToKill, 100);

			for(auto & hex : unit->getHexes())
				if(hex.isAvailable()) //towers can have <0 pos; we don't also want to overwrite side columns
					params.destructibleEnemyTurns[hex.toInt()] = turnsToKill * unit->getMovementRange();
		}

		params.bypassEnemyStacks = true;
	}

	return state->getReachability(params);
}

MoveTarget BattleExchangeEvaluator::findMoveTowardsUnreachable(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb)
{
	MoveTarget result;
	if(targets.berserk)
	{
		// A detached forecast must retain Berserk's selected path. Do not replace
		// its forced destination with the ordinary tactical target chooser.
		for(const auto & forcedAction : targets.forcedBerserkActions)
			if(forcedAction.type == EActionType::WALK && forcedAction.position.isValid())
				result.positions.insert(forcedAction.position);

		if(!result.positions.empty())
		{
			result.score = 0.0f;
			result.turnsToReach = 1;
		}
		return result;
	}

	BattleExchangeVariant ev;

	logAi->trace("Find move towards unreachable. Enemies count %d", targets.unreachableEnemies.size());

	if(targets.unreachableEnemies.empty())
		return result;

	auto speed = activeStack->getMovementRange();

	if(speed == 0)
		return result;

	updateReachabilityMap(hb);

	auto dists = getReachabilityWithEnemyBypass(activeStack, damageCache, hb);
	auto flying = activeStack->hasBonusOfType(BonusType::FLYING);

	for(const battle::Unit * enemy : targets.unreachableEnemies)
	{
		logAi->trace(
			"Checking movement towards %d of %s",
			enemy->getCount(),
			LIBRARY->creatures()->getById(enemy->creatureId())->getJsonKey());

		auto distance = distToNearestReachableNeighbour(dists, activeStack, enemy);

		if(distance >= GameConstants::BFIELD_SIZE)
			continue;

		if(distance <= speed)
			continue;

		float penaltyMultiplier = 1.0f; // Default multiplier, no penalty
		float closestAllyDistance = std::numeric_limits<float>::max();

		for (const battle::Unit* ally : hb->battleAliveUnits()) 
		{
			if (ally == activeStack || !controlledBySamePlayer(*hb, activeStack, ally))
				continue;

			float allyDistance = distToNearestReachableNeighbour(dists, ally, enemy);
			if (allyDistance < closestAllyDistance)
			{
				closestAllyDistance = allyDistance;
			}
		}

		// If an ally is closer to the enemy, compute the penaltyMultiplier
		if (closestAllyDistance < distance) 
		{
			penaltyMultiplier = closestAllyDistance / distance; // Ratio of distances
		}

		auto turnsToReach = (distance - 1) / speed + 1;
		const BattleHexArray & hexes = enemy->getAttackableHexes(activeStack);
		auto enemySpeed = enemy->getMovementRange();
		auto speedRatio = speed / static_cast<float>(enemySpeed);
		auto multiplier = (speedRatio > 1 ? 1 : speedRatio) * penaltyMultiplier;

		for(auto & hex : hexes)
		{
			if (!dists.isReachable(hex))
				continue;
			// FIXME: provide distance info for Jousting bonus
			auto bai = BattleAttackInfo(activeStack, enemy, 0, cb->battleCanShoot(activeStack));
			auto attack = AttackPossibility::evaluate(bai, hex, damageCache, hb);

			attack.shootersBlockedDmg = 0; // we do not want to count on it, it is not for sure

			auto score = calculateExchange(attack, turnsToReach, targets, damageCache, hb);

			score.enemyDamageReduce *= multiplier;

#if BATTLE_TRACE_LEVEL >= 1
			logAi->trace("Multiplier: %f, turns: %d, current score %f, new score %f", multiplier, turnsToReach, result.score, scoreValue(score));
#endif

			if(result.score < scoreValue(score)
				|| (result.turnsToReach > turnsToReach && vstd::isAlmostEqual(result.score, scoreValue(score))))
			{
				result.score = scoreValue(score);
				result.positions.clear();

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("New high score");
#endif

					BattleHex enemyHex = hex;
					while(!flying
						&& (dists.distances[enemyHex.toInt()] > speed || !dists.isReachable(enemyHex))
						&& dists.predecessors.at(enemyHex.toInt()).isValid())
					{
						enemyHex = dists.predecessors.at(enemyHex.toInt());

						if(dists.accessibility[enemyHex.toInt()] == EAccessibility::ALIVE_STACK)
						{
							auto defenderToBypass = hb->battleGetUnitByPos(enemyHex);
							assert(defenderToBypass != nullptr);
							auto attackHex = dists.predecessors[enemyHex.toInt()];
							
							if(defenderToBypass && dists.isReachable(attackHex) &&
							   defenderToBypass != enemy &&
							   vstd::contains(defenderToBypass->getAttackableHexes(activeStack), attackHex))
							{
#if BATTLE_TRACE_LEVEL >= 1
								logAi->trace("Found target to bypass at %d", enemyHex.toInt());
#endif
								
								auto baiBypass = BattleAttackInfo(activeStack, defenderToBypass, 0, cb->battleCanShoot(activeStack));
								auto attackBypass = AttackPossibility::evaluate(baiBypass, attackHex, damageCache, hb);

								auto adjacentStacks = getAdjacentUnits(enemy);

								adjacentStacks.push_back(defenderToBypass);
								vstd::removeDuplicates(adjacentStacks);

								auto bypassScore = calculateExchange(
									attackBypass,
									dists.distances[attackHex.toInt()],
									targets,
									damageCache,
									hb,
									adjacentStacks);

								if(scoreValue(bypassScore) > result.score)
								{
									result.score = scoreValue(bypassScore);

#if BATTLE_TRACE_LEVEL >= 1
									logAi->trace("New high score after bypass %f", scoreValue(bypassScore));
#endif
								}
							}
						}
					}
					if(!dists.isReachable(enemyHex))
						continue;

				result.positions.insert(enemyHex);
				result.cachedAttack = attack;
				result.turnsToReach = turnsToReach;
			}
		}
	}

	return result;
}

battle::Units BattleExchangeEvaluator::getAdjacentUnits(const battle::Unit * blockerUnit) const
{
	std::queue<const battle::Unit *> queue;
	battle::Units checkedStacks;

	queue.push(blockerUnit);

	while(!queue.empty())
	{
		auto stack = queue.front();

		queue.pop();
		checkedStacks.push_back(stack);

		auto const & hexes = stack->getSurroundingHexes();
		for(const auto & hex : hexes)
		{
			auto neighbour = cb->battleGetUnitByPos(hex);

			if(neighbour && neighbour->unitSide() == stack->unitSide() && !vstd::contains(checkedStacks, neighbour))
			{
				queue.push(neighbour);
				checkedStacks.push_back(neighbour);
			}
		}
	}

	return checkedStacks;
}

ReachabilityData BattleExchangeEvaluator::getExchangeUnits(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	std::shared_ptr<HypotheticBattle> hb,
	const battle::Units & additionalUnits) const
{
	ReachabilityData result;

	auto hexes = ap.attack.defender->getSurroundingHexes();

	if(!ap.attack.shooting) 
		hexes.insert(ap.from);

	battle::Units allReachableUnits = additionalUnits;
	
	for(const auto & hex : hexes)
	{
		vstd::concatenate(allReachableUnits, getOneTurnReachableUnits(turn, hex));
	}

	if(!ap.attack.attacker->isTurret())
	{
		for(const auto & hex : ap.attack.attacker->getHexes())
		{
			auto unitsReachingAttacker = getOneTurnReachableUnits(turn, hex);
			for(auto unit : unitsReachingAttacker)
			{
				if(unit->unitSide() != ap.attack.attacker->unitSide())
				{
					allReachableUnits.push_back(unit);
					result.enemyUnitsReachingAttacker.insert(unit->unitId());
				}
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	auto copy = allReachableUnits;
	for(auto unit : copy)
	{
		for(auto adjacentUnit : getAdjacentUnits(unit))
		{
			auto unitWithBonuses = hb->battleGetUnitByID(adjacentUnit->unitId());

			if(vstd::contains(targets.unreachableEnemies, adjacentUnit)
				&& !vstd::contains(allReachableUnits, unitWithBonuses))
			{
				allReachableUnits.push_back(unitWithBonuses);
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	if(!vstd::contains(allReachableUnits, ap.attack.attacker))
	{
		allReachableUnits.push_back(ap.attack.attacker);
	}

	if(allReachableUnits.size() < 2)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Reachability map contains only %d stacks", allReachableUnits.size());
#endif

		return result;
	}

	for(auto unit : allReachableUnits)
	{
		auto accessible = !unit->canShoot() || vstd::contains(additionalUnits, unit);

		if(!accessible)
		{
			for(const auto & hex : unit->getSurroundingHexes())
			{
				if(ap.attack.defender->coversPos(hex))
				{
					accessible = true;
				}
			}
		}

		if(accessible)
			result.melleeAccessible.push_back(unit);
		else
			result.shooters.push_back(unit);
	}

	for(int turn = 0; turn < turnOrder.size(); turn++)
	{
		for(auto unit : turnOrder[turn])
		{
			if(vstd::contains(allReachableUnits, unit))
				result.units[turn].push_back(unit);
		}

		vstd::erase_if(result.units[turn], [&](const battle::Unit * u) -> bool
			{
				return !hb->battleGetUnitByID(u->unitId())->alive();
			});
	}

	return result;
}

float BattleExchangeEvaluator::evaluateExchange(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb) const
{
	BattleScore score = calculateExchange(ap, turn, targets, damageCache, hb);

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace(
		"calculateExchange score +%2f -%2fx%2f = %2f",
		score.enemyDamageReduce,
		score.ourDamageReduce,
		getNegativeEffectMultiplier(),
		scoreValue(score));
#endif

	return scoreValue(score);
}

BattleScore BattleExchangeEvaluator::calculateExchange(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	const battle::Units & additionalUnits) const
{
#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Battle exchange at %d", ap.attack.shooting ? ap.dest.toInt() : ap.from.toInt());
#endif

	if(cb->battleGetMySide() == BattleSide::LEFT_SIDE
		&& cb->battleGetGateState() == EGateState::BLOCKED
		&& ap.attack.defender->coversPos(BattleHex::GATE_BRIDGE))
	{
		return BattleScore(EvaluationResult::INEFFECTIVE_SCORE, 0);
	}

	battle::Units ourStacks;
	battle::Units enemyStacks;

	if(hb->battleGetUnitByID(ap.attack.defender->unitId())->alive())
		enemyStacks.push_back(ap.attack.defender);

	ReachabilityData exchangeUnits = getExchangeUnits(ap, turn, targets, hb, additionalUnits);

	if(exchangeUnits.units.empty())
	{
		return BattleScore();
	}

	auto exchangeBattle = std::make_shared<HypotheticBattle>(env.get(), hb);
	BattleExchangeVariant v;

	for(int exchangeTurn = 0; exchangeTurn < exchangeUnits.units.size(); exchangeTurn++)
	{
		for(auto unit : exchangeUnits.units.at(exchangeTurn))
		{
			if(unit->isTurret())
				continue;

			bool isOur = exchangeBattle->battleMatchOwner(ap.attack.attacker, unit, true);
			auto & attackerQueue = isOur ? ourStacks : enemyStacks;
			auto u = exchangeBattle->getForUpdate(unit->unitId());

			if(u->alive() && !vstd::contains(attackerQueue, unit))
			{
				attackerQueue.push_back(unit);

#if BATTLE_TRACE_LEVEL
				logAi->trace("Exchanging: %s", u->getDescription());
#endif
			}
		}
	}

	auto melleeAttackers = ourStacks;

	vstd::removeDuplicates(melleeAttackers);
	vstd::erase_if(melleeAttackers, [&](const battle::Unit * u) -> bool
		{
			return cb->battleCanShoot(u);
		});

	bool canUseAp = true;

	std::set<uint32_t> blockedShooters;

	int totalTurnsCount = simulationTurnsCount >= turn + turnOrder.size()
		? simulationTurnsCount
		: turn + turnOrder.size();

	for(int exchangeTurn = 0; exchangeTurn < simulationTurnsCount; exchangeTurn++)
	{
		bool isMovingTurm = exchangeTurn < turn;
		int queueTurn = exchangeTurn >= exchangeUnits.units.size()
			? exchangeUnits.units.size() - 1
			: exchangeTurn;

		for(auto activeUnit : exchangeUnits.units.at(queueTurn))
		{
			bool isOur = exchangeBattle->battleMatchOwner(ap.attack.attacker, activeUnit, true);
			battle::Units & attackerQueue = isOur ? ourStacks : enemyStacks;
			battle::Units & oppositeQueue = isOur ? enemyStacks : ourStacks;

			auto attacker = exchangeBattle->getForUpdate(activeUnit->unitId());
			auto shooting = exchangeBattle->battleCanShoot(attacker.get())
				&& !vstd::contains(blockedShooters, attacker->unitId());

			if(!attacker->alive())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace("Attacker is dead");
#endif

				continue;
			}

			// The first AP is the already-active unit's current action. Replaying
			// nextTurn here would clear gifts that were armed before this exchange.
			// Every later queue entry is a genuine activation and may consume a
			// pending Cascade gift.
			const bool initialChosenUnit = canUseAp
				&& activeUnit->unitId() == ap.attack.attacker->unitId();
			if(!initialChosenUnit)
				exchangeBattle->nextTurn(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			bool fortuneActivation = true;
			auto finishFortuneActivation = [&]()
			{
				if(fortuneActivation)
				{
					exchangeBattle->endFortuneActivation();
					fortuneActivation = false;
				}
			};

			if(isMovingTurm && !shooting
				&& !vstd::contains(exchangeUnits.enemyUnitsReachingAttacker, attacker->unitId()))
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace("Attacker is moving");
#endif
				finishFortuneActivation();
				continue;
			}

			auto targetUnit = ap.attack.defender;

			if(!isOur || !exchangeBattle->battleGetUnitByID(targetUnit->unitId())->alive())
			{
#if BATTLE_TRACE_LEVEL>=2
				logAi->trace("Best target selector for %s", attacker->getDescription());
#endif
				auto estimateAttack = [&](const battle::Unit * u) -> float
				{
					auto stackWithBonuses = exchangeBattle->getForUpdate(u->unitId());
					auto score = v.trackAttack(
						attacker,
						stackWithBonuses,
						shooting,
						isOur,
						damageCache,
						exchangeBattle,
						true);

#if BATTLE_TRACE_LEVEL>=2
					logAi->trace("Best target selector %s->%s score = %2f", attacker->getDescription(), stackWithBonuses->getDescription(), score);
#endif

					return score;
				};
				auto selectBestTarget = [&](const battle::Units & candidates) -> const battle::Unit *
				{
					if(candidates.empty())
						return nullptr;

					const battle::Unit * best = candidates.front();
					// maxElementByFun never invokes its comparator for one element.
					if(candidates.size() == 1)
						return best;

					float bestScore = estimateAttack(best);
					for(size_t index = 1; index < candidates.size(); ++index)
					{
						const auto * candidate = candidates[index];
						const float candidateScore = estimateAttack(candidate);
						if(candidateScore > bestScore)
						{
							best = candidate;
							bestScore = candidateScore;
						}
					}
					return best;
				};

				auto unitsInOppositeQueueExceptInaccessible = oppositeQueue;

				vstd::erase_if(unitsInOppositeQueueExceptInaccessible, [&](const battle::Unit * u)->bool
					{
						return vstd::contains(exchangeUnits.shooters, u);
					});

				if(!isOur
					&& exchangeTurn == 0
					&& exchangeUnits.units.at(exchangeTurn).at(0)->unitId() != ap.attack.attacker->unitId()
					&& !vstd::contains(exchangeUnits.enemyUnitsReachingAttacker, attacker->unitId()))
				{
					vstd::erase_if(unitsInOppositeQueueExceptInaccessible, [&](const battle::Unit * u) -> bool
						{
							return u->unitId() == ap.attack.attacker->unitId();
						});
				}

				if(!unitsInOppositeQueueExceptInaccessible.empty())
				{
					targetUnit = selectBestTarget(unitsInOppositeQueueExceptInaccessible);
				}
				else
				{
					auto reachable = exchangeBattle->battleGetUnitsIf([this, &exchangeBattle, &attacker](const battle::Unit * u) -> bool
						{
							if(u->unitSide() == attacker->unitSide())
								return false;

							if(!exchangeBattle->getForUpdate(u->unitId())->alive())
								return false;

							if(!u->getPosition().isValid())
								return false; // e.g. tower shooters

							const auto & reachableUnits = getOneTurnReachableUnits(0, u->getPosition());

							return vstd::contains_if(reachableUnits, [&attacker](const battle::Unit * other) -> bool
								{
									return attacker->unitId() == other->unitId();
								});
						});

					if(!reachable.empty())
					{
						targetUnit = selectBestTarget(reachable);
					}
					else
					{
#if BATTLE_TRACE_LEVEL>=1
						logAi->trace("Battle queue is empty and no reachable enemy.");
#endif
						finishFortuneActivation();
						continue;
					}
				}
			}

			auto defender = exchangeBattle->getForUpdate(targetUnit->unitId());
			const int totalAttacks = AttackPossibility::getAttackCount(*attacker, shooting, *exchangeBattle);

			if(canUseAp && activeUnit->unitId() == ap.attack.attacker->unitId()
				&& targetUnit->unitId() == ap.attack.defender->unitId())
			{
				v.trackAttack(ap, exchangeBattle, damageCache);
			}
			else
			{
				for(int i = 0; i < totalAttacks; i++)
				{
					v.trackAttack(attacker, defender, shooting, isOur, damageCache, exchangeBattle, false, i == 0);

					if(!attacker->alive() || !defender->alive())
						break;
				}
			}

			if(!shooting)
				blockedShooters.insert(defender->unitId());
			finishFortuneActivation();

			canUseAp = false;

			vstd::erase_if(attackerQueue, [&](const battle::Unit * u) -> bool
				{
					return !exchangeBattle->battleGetUnitByID(u->unitId())->alive();
				});

			vstd::erase_if(oppositeQueue, [&](const battle::Unit * u) -> bool
				{
					return !exchangeBattle->battleGetUnitByID(u->unitId())->alive();
				});
		}

		exchangeBattle->nextRound();
	}

	auto score = v.getScore();

	if(simulationTurnsCount < totalTurnsCount)
	{
		float scalingRatio = simulationTurnsCount / static_cast<float>(totalTurnsCount);

		score.enemyDamageReduce *= scalingRatio;
		score.ourDamageReduce *= scalingRatio;
	}

	if(turn > 0)
	{
		auto turnMultiplier = 1 - std::min(0.2, 0.05 * turn);

		score.enemyDamageReduce *= turnMultiplier;
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Exchange score: enemy: %2f, our -%2f", score.enemyDamageReduce, score.ourDamageReduce);
#endif

	return score;
}

bool BattleExchangeEvaluator::canBeHitThisTurn(const AttackPossibility & ap)
{
	for(auto pos : ap.attack.attacker->getSurroundingHexes())
	{
		for(auto u : getOneTurnReachableUnits(0, pos))
		{
			if(u->unitSide() != ap.attack.attacker->unitSide())
			{
				return true;
			}
		}
	}

	return false;
}

void ReachabilityMapCache::update(const std::vector<battle::Units> & turnOrder, std::shared_ptr<HypotheticBattle> hb)
{
	for(auto turn : turnOrder)
	{
		for(auto u : turn)
		{
			if(!vstd::contains(unitReachabilityMap, u->unitId()))
			{
				unitReachabilityMap[u->unitId()] = hb->getReachability(u);
			}
		}
	}

	hexReachabilityPerTurn.clear();
}

void BattleExchangeEvaluator::updateReachabilityMap(std::shared_ptr<HypotheticBattle> hb)
{
	const int TURN_DEPTH = 2;

	turnOrder.clear();

	hb->battleGetTurnOrder(turnOrder, std::numeric_limits<int>::max(), TURN_DEPTH);
	reachabilityMap.update(turnOrder, hb);
}

const battle::Units & ReachabilityMapCache::getOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	auto & turnData = hexReachabilityPerTurn[turn];

	if (!turnData.isValid[hex.toInt()])
	{
		turnData.hexes[hex.toInt()] = computeOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
		turnData.isValid.set(hex.toInt());
	}

	return turnData.hexes[hex.toInt()];
}

battle::Units ReachabilityMapCache::computeOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	battle::Units result;

	for(int i = 0; i < turnOrder.size(); i++, turn++)
	{
		auto & turnQueue = turnOrder[i];
		HypotheticBattle turnBattle(env.get(), cb);

		for(const battle::Unit * unit : turnQueue)
		{
			if(unit->isTurret())
				continue;

			if(turnBattle.battleCanShoot(unit))
			{
				result.push_back(unit);

				continue;
			}

			auto unitSpeed = unit->getMovementRange(turn);
			auto radius = unitSpeed * (turn + 1);

			auto reachabilityIter = unitReachabilityMap.find(unit->unitId());
			assert(reachabilityIter != unitReachabilityMap.end()); // missing updateReachabilityMap call?

			ReachabilityInfo unitReachability = reachabilityIter != unitReachabilityMap.end() ? reachabilityIter->second : turnBattle.getReachability(unit);

			// Distances may be finite through Passing Lines (or Ghost Walk) even
			// when this occupied hex is not a legal movement destination.
			bool reachable = unitReachability.isReachable(hex)
				&& unitReachability.distances.at(hex.toInt()) <= radius;

			if(!reachable && unitReachability.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
			{
				const battle::Unit * hexStack = turnBattle.battleGetUnitByPos(hex);

				if(hexStack && controlledByDifferentPlayer(turnBattle, unit, hexStack))
				{
					for(const BattleHex & neighbour : hex.getNeighbouringTiles())
					{
						reachable = unitReachability.isReachable(neighbour)
							&& unitReachability.distances.at(neighbour.toInt()) <= radius;

						if(reachable) break;
					}
				}
			}

			if(reachable)
			{
				result.push_back(unit);
			}
		}
	}

	return result;
}

const battle::Units & BattleExchangeEvaluator::getOneTurnReachableUnits(uint8_t turn, const BattleHex & hex) const
{
	return reachabilityMap.getOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
}

// avoid blocking path for stronger stack by weaker stack
bool BattleExchangeEvaluator::checkPositionBlocksOurStacks(const HypotheticBattle & hb, const battle::Unit * activeUnit, const BattleHex & position)
{
	const int BLOCKING_THRESHOLD = 70;
	const int BLOCKING_OWN_ATTACK_PENALTY = 100;
	const int BLOCKING_OWN_MOVE_PENALTY = 1;

	float blockingScore = 0;

	auto activeUnitDamage = activeUnit->getMinDamage(hb.battleCanShoot(activeUnit)) * activeUnit->getCount();

	for(int turn = 0; turn < turnOrder.size(); turn++)
	{
		auto & turnQueue = turnOrder[turn];
		HypotheticBattle turnBattle(env.get(), cb);

		auto unitToUpdate = turnBattle.getForUpdate(activeUnit->unitId());
		unitToUpdate->setPosition(position);

		for(const battle::Unit * unit : turnQueue)
		{
			if(unit->unitId() == unitToUpdate->unitId()
				|| controlledByDifferentPlayer(turnBattle, unit, activeUnit))
				continue;

			auto blockedUnitDamage = unit->getMinDamage(hb.battleCanShoot(unit)) * unit->getCount();
			float ratio = blockedUnitDamage / (float)(blockedUnitDamage + activeUnitDamage + 0.01);

			auto unitReachability = turnBattle.getReachability(unit);
			auto unitSpeed = unit->getMovementRange(turn); // Cached value, to avoid performance hit

			for(BattleHex hex = BattleHex::TOP_LEFT; hex.isValid(); ++hex)
			{
				bool enemyUnit = false;
				// A transit-occupied hex must not be scored as a legal endpoint.
				bool reachable = unitReachability.isReachable(hex)
					&& unitReachability.distances.at(hex.toInt()) <= unitSpeed;

				if(!reachable && unitReachability.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
				{
					const battle::Unit * hexStack = turnBattle.battleGetUnitByPos(hex);

					if(hexStack && controlledByDifferentPlayer(turnBattle, unit, hexStack))
					{
						enemyUnit = true;
						for(const BattleHex & neighbour : hex.getNeighbouringTiles())
						{
							reachable = unitReachability.isReachable(neighbour)
								&& unitReachability.distances.at(neighbour.toInt()) <= unitSpeed;

							if(reachable) break;
						}
					}
				}

				if(!reachable)
				{
					auto reachableUnits = getOneTurnReachableUnits(0, hex);
					if (std::count(reachableUnits.begin(), reachableUnits.end(), unit) > 1)
						blockingScore += ratio * (enemyUnit ? BLOCKING_OWN_ATTACK_PENALTY : BLOCKING_OWN_MOVE_PENALTY);
				}
			}
		}
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Position %d, blocking score %f", position.toInt(), blockingScore);
#endif

	return blockingScore > BLOCKING_THRESHOLD;
}
