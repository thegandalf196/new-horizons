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
#include "../../lib/battle/NewHorizonsOpportunist.h"

SelectedAttackReceipt::SelectedAttackReceipt(const AttackPossibility & preview)
	: queryOwner(preview.queryWorld), effectOwner(preview.effectPreview),
	actorSnapshot(preview.attackerState), recipients(preview.affectedUnits)
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
			(before = unitToUpdate->getAvailableHealth()) - (projected = affectedUnit->getAvailableHealth()));
		observations.push_back({id, before, projected});
	}
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
	unitToUpdate->archeryCounterfireRound = captured.archeryCounterfireRound;
	unitToUpdate->archeryCrossfireRound = captured.archeryCrossfireRound;
	unitToUpdate->archeryCrossfireAttackers = captured.archeryCrossfireAttackers;
	unitToUpdate->archeryCrossfireDefenders = captured.archeryCrossfireDefenders;
	unitToUpdate->battlecraftWaitBonusUsed = captured.battlecraftWaitBonusUsed;
	unitToUpdate->battlecraftWaitMasteryDoubled = captured.battlecraftWaitMasteryDoubled;
	unitToUpdate->battlecraftDefendMasteryDoubled = captured.battlecraftDefendMasteryDoubled;
	unitToUpdate->battlecraftPreemptiveStrikeRound = captured.battlecraftPreemptiveStrikeRound;
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
		&& newHorizonsOpportunist::hasPerk(branch, unitToUpdate.get()))
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
