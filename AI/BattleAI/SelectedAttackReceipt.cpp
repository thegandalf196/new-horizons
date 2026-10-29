/*
 * SelectedAttackReceipt.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SelectedAttackReceipt.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "VisibleCandidateWorld.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"

SelectedAttackReceipt::SelectedAttackReceipt(const AttackPossibility & preview)
	: queryOwner(preview.queryWorld), effectOwner(preview.effectPreview),
	actorSnapshot(preview.attackerState), recipients(preview.affectedUnits),
	soulHarvestHealthGained(preview.soulHarvestHealthGained)
{
}

void SelectedAttackReceipt::captureDamage(HypotheticBattle & branch)
{
	recipients.push_back(actorSnapshot);
	for(const auto & affectedUnit : recipients)
	{
		auto unitToUpdate = branch.getForUpdate(affectedUnit->unitId());
		int64_t before, projected;
		uint32_t id;
		// Retain the original subtraction's operand/query sequencing contract.
		finalDamage[id = unitToUpdate->unitId()] = std::max<int64_t>(0,
			(before = unitToUpdate->getAvailableHealth()) - (projected = affectedUnit->getAvailableHealth())
			+ (unitToUpdate->unitId() == actorSnapshot->unitId() ? soulHarvestHealthGained : 0));
		observations.push_back({id, before, projected});
	}
}

void selectedAttackReceipt::restorePosition(const AttackPossibility & ap, battle::CUnitState & actor,
	const HypotheticBattle & branch)
{
	if(!ap.from.isValid() || !actor.alive())
		return;
	if(ap.attack.shooting)
	{
		if(actor.getPosition() == ap.from)
			return;
		if(!ap.attackerState || !ap.attack.attacker || !ap.attack.defender
			|| ap.attack.archeryRangedDamageMultiplierPercent != newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT
			|| ap.attackerState->unitId() != actor.unitId() || ap.attackerState->getPosition() != ap.from
			|| ap.attack.attacker->unitId() != actor.unitId()
			|| branch.battleGetUnitByID(actor.unitId()) != &actor)
			return;
		if(ap.queryWorld)
		{
			const auto & source = ap.queryWorld->callback();
			if(source->battleGetUnitByID(actor.unitId()) != ap.attack.attacker
				|| source->battleGetUnitByID(ap.attack.defender->unitId()) != ap.attack.defender)
				return;
		}
		const auto * target = branch.battleGetUnitByID(ap.attack.defender->unitId());
		if(!target || !branch.battleCanSkirmisherAttackFromHex(&actor, target->getPosition(), ap.from))
			return;
	}
	actor.setPosition(ap.from);
}

void selectedAttackReceipt::restoreResources(const AttackPossibility & ap, const battle::CUnitState & captured,
	const std::shared_ptr<StackWithBonuses> & unitToUpdate,
	const std::shared_ptr<StackWithBonuses> & originalActor, HypotheticBattle & branch)
{
	// The preview may contain several strikes or consume only one of several
	// retaliations. Copy consumption, not a boolean can-retaliate transition.
	// CAmmo assignment keeps this unit's owner-bound bonus caches intact.
	static_cast<battle::CAmmo &>(unitToUpdate->shots) = captured.shots;
	static_cast<battle::CAmmo &>(unitToUpdate->counterAttacks) = captured.counterAttacks;
	// Preserve the physical reaction's round receipt and target provenance
	// when this selected preview becomes a later exchange's starting state.
	unitToUpdate->aerialRetaliationRound = captured.aerialRetaliationRound;
	unitToUpdate->dreadCavalryState = captured.dreadCavalryState;
	unitToUpdate->archeryCounterfireRound = captured.archeryCounterfireRound;
	unitToUpdate->archeryCrossfireRound = captured.archeryCrossfireRound;
	unitToUpdate->archeryCrossfireAttackers = captured.archeryCrossfireAttackers;
	unitToUpdate->archeryCrossfireDefenders = captured.archeryCrossfireDefenders;
	unitToUpdate->forestAmbush = captured.forestAmbush;
	unitToUpdate->battlecraftWaitBonusUsed = captured.battlecraftWaitBonusUsed;
	unitToUpdate->battlecraftWaitMasteryDoubled = captured.battlecraftWaitMasteryDoubled;
	unitToUpdate->battlecraftDefendMasteryDoubled = captured.battlecraftDefendMasteryDoubled;
	unitToUpdate->battlecraftPreemptiveStrikeRound = captured.battlecraftPreemptiveStrikeRound;
	unitToUpdate->petrifyingBloodRound = captured.petrifyingBloodRound;
	unitToUpdate->battlecraftOverwatchReadyRound = captured.battlecraftOverwatchReadyRound;
	unitToUpdate->battlecraftOverwatchUsedRound = captured.battlecraftOverwatchUsedRound;
	unitToUpdate->cleaveUsedThisActivation = captured.cleaveUsedThisActivation;
	unitToUpdate->bulwarkPreemptiveUsed = captured.bulwarkPreemptiveUsed;
	unitToUpdate->bulwarkMireGripApplied = captured.bulwarkMireGripApplied;
	unitToUpdate->bulwarkDefendPhysicalDamage = captured.bulwarkDefendPhysicalDamage;
	unitToUpdate->bulwarkImmovableRound = captured.bulwarkImmovableRound;
	unitToUpdate->armorerBastionRound = captured.armorerBastionRound;
	unitToUpdate->armorerLastStandEndedActivation = captured.armorerLastStandEndedActivation;
	unitToUpdate->armorerLastStandDefending = captured.armorerLastStandDefending;
	unitToUpdate->bulwarkToxicSpinesRound = captured.bulwarkToxicSpinesRound;
	unitToUpdate->physicalPoisonBaseDamage = captured.physicalPoisonBaseDamage;
	unitToUpdate->physicalPoisonActivationsRemaining = captured.physicalPoisonActivationsRemaining;
	unitToUpdate->physicalPoisonSourceStackId = captured.physicalPoisonSourceStackId;
	unitToUpdate->guardianSpiritHitPoints = captured.guardianSpiritHitPoints;
	unitToUpdate->guardianSpiritRoundsRemaining = captured.guardianSpiritRoundsRemaining;
	unitToUpdate->luckyOwnAttackSequence = captured.luckyOwnAttackSequence;
	if(unitToUpdate->unitId() == originalActor->unitId()
		&& (newHorizonsOpportunist::hasPerk(branch, unitToUpdate.get())
			|| captured.dreadCavalryState.usedThisActivation))
		unitToUpdate->pursuitMovementRemaining = captured.pursuitMovementRemaining;
	unitToUpdate->rangedFollowUpDamagePercent = captured.rangedFollowUpDamagePercent;
	// The forecasted continuation is one accepted shot. Consume its copied
	// allowance here so this detached branch cannot price another reduced
	// shot on the same activation or carry it into the next one.
	if(unitToUpdate->unitId() == originalActor->unitId()
		&& ap.attack.shooting && ap.attack.physicalDamage
		&& unitToUpdate->rangedFollowUpDamagePercent > 0)
		unitToUpdate->rangedFollowUpDamagePercent = 0;
	// The detached forecast records explicitly classified hits and reactions
	// even when they are not represented by a Fortune strike. Copy its total
	// interval rather than guessing that residual HP loss was physical.
	unitToUpdate->veteranPhysicalDamageSinceActivation =
		captured.veteranPhysicalDamageSinceActivation;

}

void selectedAttackReceipt::applyCapturedDamage(HypotheticBattle & branch,
	const std::shared_ptr<StackWithBonuses> & recipient, int64_t damage)
{
	const bool wasAlive = recipient->alive();
	const auto rebirthSource = wasAlive ? branch.captureElementalRebirthSource(*recipient) : std::nullopt;
	const bool nativeRebirth = branch.hasReadyNativeRebirth(recipient.get());
	recipient->damage(damage);
	if(wasAlive && !recipient->alive())
		branch.releaseSerpentBindingsForDeath(*recipient);
	branch.recordBloodrageTransition(recipient, wasAlive);
	if(rebirthSource)
		branch.projectElementalRebirth(recipient.get(), *rebirthSource,
			wasAlive && !recipient->alive(), recipient->isClone(), nativeRebirth);
}

int64_t selectedAttackReceipt::grantSoulHarvest(battle::CUnitState & actor, int64_t killedCreatures)
{
	const auto before = actor.getAvailableHealth();
	return newHorizonsSoulHarvest::grant(actor, killedCreatures)
		? actor.getAvailableHealth() - before : 0;
}

std::unique_ptr<AttackPossibility> selectedAttackReceipt::refreshForestAmbush(
	const AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & branch,
	DamageCache & damageCache)
{
	const auto * captured = dynamic_cast<const battle::CUnitState *>(preview.attack.attacker);
	const auto * actor = branch->battleGetUnitByID(preview.attack.attacker->unitId());
	const auto * current = dynamic_cast<const battle::CUnitState *>(actor);
	if(!captured || !current || captured->forestAmbush == current->forestAmbush)
		return {};
	const auto * target = branch->battleGetUnitByID(preview.attack.defender->unitId());
	if(!actor->alive() || !target || !target->alive())
		return {};
	auto attack = preview.attack;
	attack.attacker = actor;
	attack.defender = target;
	return std::make_unique<AttackPossibility>(AttackPossibility::evaluate(
		attack, preview.from, damageCache, branch));
}

BattleAttackInfo selectedAttackReceipt::attackForStrike(const FortuneStrikeProjection & strike,
	const battle::Unit * actor, const battle::Unit * target)
{
	BattleAttackInfo attack(actor, target, 0, strike.shooting);
	attack.physicalDamage = strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE;
	attack.retaliation = strike.retaliation;
	attack.dreadCavalryDamagePercent = strike.dreadCavalryDamagePercent;
	attack.bracePreemptive = strike.bracePreemptive;
	attack.cleaveDamagePercent = strike.cleaveDamagePercent;
	attack.attackerPos = actor->getPosition();
	attack.defenderPos = target->getPosition();
	return attack;
}
