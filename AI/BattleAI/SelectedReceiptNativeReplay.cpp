/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SelectedAttackReceipt.h"
#include "FocusMagicProjection.h"
#include "AttackPossibility.h"
#include "BattleShroudStrike.h"
#include "GnollHarriersProjection.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include <algorithm>

void focusMagicAI::restoreAcceptedRecords(StackWithBonuses & target, const battle::CUnitState & captured)
{
	// Wholesale CUnitState assignment does not copy provider overlays. Import
	// the accepted local Focus receipt; never consume another charge or shot.
	const auto accepted = newHorizonsFocusMagic::nativeRecords(captured);
	const auto current = newHorizonsFocusMagic::nativeRecords(target);
	if(accepted.empty() && current.empty()) return;
	auto spells = target.localSpellEffects();
	std::erase_if(spells, newHorizonsFocusMagic::isEnchantmentRecord);
	spells.insert(spells.end(), accepted.begin(), accepted.end());
	target.replaceLocalSpellEffectsExact(spells);
}

BattleHex selectedAttackReceipt::replayMovementNative(const AttackPossibility & ap, battle::CUnitState & actor,
	HypotheticBattle & branch)
{
	const auto originalPosition = actor.getPosition();
	const bool replayVoluntaryApproach = gnollHarriersAI::replaysSameApproach(ap, actor);
	applySelectedNightProwler(branch, actor, originalPosition, ap.from);
	if(!ap.overwatchHits.empty())
		branch.projectVoluntaryMovement(actor.unitId(), ap.from, replayVoluntaryApproach);
	selectedAttackReceipt::restorePosition(ap, actor, branch);
	if(replayVoluntaryApproach)
		gnollHarriersAI::record(branch, actor.unitId(), originalPosition, actor.getPosition());
	return originalPosition;
}

void selectedAttackReceipt::replayResourcesNative(const AttackPossibility & ap, const battle::CUnitState & captured,
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
	unitToUpdate->sylvanAmbush = captured.sylvanAmbush;
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
	unitToUpdate->physicalPoisonSpellSide = captured.physicalPoisonSpellSide;
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
