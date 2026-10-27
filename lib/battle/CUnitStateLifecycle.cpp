/*
 * CUnitStateLifecycle.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CUnitState.h"

// Exact existing initialization, action/round boundaries and retirement hooks.
// Layout, copy, JSON persistence and environment binding remain with their owners.
namespace battle
{
void CUnitState::localInit(const IUnitEnvironment * env_)
{
	bindStateEnvironment(env_);
	reset();
	health.init();
}

void CUnitState::reset()
{
	heroicSpiritRetaliation = false;
	heroicSpiritMoralePending = false;
	confusionState = {};
	frozenAppliedRound = -1;
	battlecraftOverwatchReadyRound = -1;
	battlecraftOverwatchUsedRound = -1;
	cloned = false;
	personalBloodrageIncrement = 0;
	veteranCohesionEarned = false;
	activationMovementBonus = 0;
	defending = false;
	drainedMana = false;
	fear = false;
	hadMorale = false;
	moraleExtraActivation = false;
	castSpellThisTurn = false;
	ghost = false;
	ghostPending = false;
	movedThisRound = false;
	timeStopTurnConsumedFlag = false;
	rangedFollowUpDamagePercent = 0;
	luckyOwnAttackSequence = false;
	regenerationRateMillionths = 0;
	regenerationPendingMicroHealth = 0;
	noQuarterMoraleActivationsRemaining = 0;
	capacityRegenerationRemainderTenths = 0;
	summoned = false;
	summonRoundsRemaining = 0;
	natureSummoned = false;
	waiting = false;
	waitedThisTurn = false;
	battlecraftWaitBonusUsed = false;
	battlecraftWaitMasteryDoubled = false;
	battlecraftPreemptiveStrikeRound = -1;
	defensiveStanceMeleeBonus = 0;
	defensiveStanceRangedBonus = 0;
	battlecraftDefendMasteryDoubled = false;
	armorerLastStandEndedActivation = false;
	armorerLastStandDefending = false;
	bulwarkPreemptiveUsed = false;
	bulwarkMireGripApplied = false;
	bulwarkDefendPhysicalDamage = 0;
	veteranPhysicalDamageSinceActivation = 0;
	bulwarkImmovableRound = -1;
	armorerBastionRound = -1;
	bulwarkToxicSpinesRound = -1;
	physicalPoisonBaseDamage = 0;
	physicalPoisonActivationsRemaining = 0;
	physicalPoisonSourceStackId = -1;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomInitialIntegrity = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	phantomShadowGiftMaximumHealthLost = 0;
	capacityHealthReferenceMax = 0;
	battleFormOriginalHealth.reset();
	battleFormCreatureId = CreatureID(-1);
	battleFormOriginalCreatureId = CreatureID(-1);
	battleFormRoundsRemaining = 0;
	battleFormRestorationPending = false;
	battleFormOriginalMaxHealth = 0;
	battleFormOriginalCount = 0;
	battleFormInitiativeSnapshot = 0;
	battleFormInitiativeSnapshotActive = false;
	battleFormOriginalCapacityHealthReferenceMax = 0;
	battleFormOriginalCapacityRegenerationRemainderTenths = 0;

	casts.reset();
	counterAttacks.reset();
	health.reset();
	shots.reset();

	cloneID = -1;

	position = BattleHex::INVALID;
	archeryCounterfireRound = -1;
	archeryDeadeyeRound = -1;
	archerySuppressionActivationSerial = -1;
	archeryRainOfArrowsActivationSerial = -1;
	archeryCrossfireRound = -1;
	archeryCrossfireAttackers.clear();
	archeryCrossfireDefenders.clear();
}

void CUnitState::afterAttack(bool ranged, bool counter, bool physical)
{
	if(counter)
		counterAttacks.use();

	if(ranged)
		shots.use();

	if(physical && waitedThisTurn)
	{
		battlecraftWaitBonusUsed = true;
		battlecraftWaitMasteryDoubled = false;
	}
}

void CUnitState::afterWait()
{
	// Waiting is a once-per-round action.  Keep an already-spent bonus spent if
	// a malformed/repeated request reaches the state visitor during the same
	// round; the authoritative action validator remains responsible for rejecting
	// that request, but state application must not re-arm the effect.
	if(!waitedThisTurn)
	{
		battlecraftWaitBonusUsed = false;
		battlecraftWaitMasteryDoubled = false;
	}
	waiting = true;
	waitedThisTurn = true;
}

void CUnitState::afterNewRound(bool isFirstRound, bool deferBattleFormRestoration, bool pauseBattleForm)
{
	moraleExtraActivation = false;
	battlecraftOverwatchReadyRound = -1;
	battlecraftOverwatchUsedRound = -1;
	if(!isFirstRound && hasBattleForm())
	{
		// The preserved initiative only represents the remainder of the round
		// in which the form was cast. Stasis pauses form lifetime, not the round.
		battleFormInitiativeSnapshotActive = false;
		if(!isTimeStopped() && !pauseBattleForm)
		{
			if(battleFormRoundsRemaining <= 1)
			{
				if(!deferBattleFormRestoration)
					endBattleForm();
			}
			else
				--battleFormRoundsRemaining;
		}
	}

	if(!isFirstRound && summonRoundsRemaining > 0 && !isTimeStopped()
		&& --summonRoundsRemaining == 0)
		makeGhost();

	if(!isFirstRound && phantomInitialIntegrity > 0 && phantomIntegrity > 0
		&& phantomRoundsRemaining > 0 && !isTimeStopped())
	{
		if(--phantomRoundsRemaining == 0)
			makeGhost();
	}

	// Defend's action flag blocks movement only for this round. Its separate
	// UNIT_DEFENDING bonus remains the authoritative stance marker until the
	// stack's next activation, so retain the captured stance values here.
	defending = false;
	activationMovementBonus = 0;
	bulwarkPreemptiveUsed = false;
	waiting = false;
	waitedThisTurn = false;
	battlecraftWaitBonusUsed = false;
	battlecraftWaitMasteryDoubled = false;
	timeStopTurnConsumedFlag = false;
	movedThisRound = false;
	pursuitMovementRemaining = 0;
	cleaveUsedThisActivation = false;
	rangedFollowUpDamagePercent = 0;
	luckyOwnAttackSequence = false;
	archeryCounterfireRound = -1;
	hadMorale = false;
	castSpellThisTurn = false;
	fear = false;
	drainedMana = false;
	counterAttacks.reset();

	if(alive() && isClone() && !bonusCache.hasBonus(UnitBonusValuesProxy::CLONE_MARKER))
		makeGhost();
}

void CUnitState::afterGetsTurn(BattleUnitTurnReason reason)
{
	battlecraftOverwatchReadyRound = -1;
	// The corresponding STACK_GETS_TURN bonuses are removed by BattleInfo just
	// before this hook.  Clear the explicit provenance alongside them; it must
	// never survive merely because another temporary bonus used the same
	// duration.
	defending = false;
	defensiveStanceMeleeBonus = 0;
	defensiveStanceRangedBonus = 0;
	battlecraftDefendMasteryDoubled = false;
	if(reason == BattleUnitTurnReason::MORALE)
	{
		hadMorale = true;
		castSpellThisTurn = false;
		movedThisRound = false;
	}
	else if(reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION)
	{
		// Quartermaster creates a fresh action window, but it is not Morale and
		// does not consume or refresh per-round waiting benefits.
		castSpellThisTurn = false;
		movedThisRound = false;
		waiting = false;
	}
}

void CUnitState::makeGhost()
{
	summonRoundsRemaining = 0;
	moraleExtraActivation = false;
	heroicSpiritRetaliation = false;
	heroicSpiritMoralePending = false;
	battlecraftOverwatchReadyRound = -1;
	endBattleForm();
	activationMovementBonus = 0;
	pursuitMovementRemaining = 0;
	cleaveUsedThisActivation = false;
	rangedFollowUpDamagePercent = 0;
	luckyOwnAttackSequence = false;
	battlecraftWaitMasteryDoubled = false;
	battlecraftDefendMasteryDoubled = false;
	armorerLastStandEndedActivation = false;
	armorerLastStandDefending = false;
	veteranPhysicalDamageSinceActivation = 0;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	health.reset();
	ghostPending = true;
}

void CUnitState::onRemoved()
{
	summonRoundsRemaining = 0;
	heroicSpiritRetaliation = false;
	heroicSpiritMoralePending = false;
	endBattleForm();
	activationMovementBonus = 0;
	rangedFollowUpDamagePercent = 0;
	luckyOwnAttackSequence = false;
	armorerLastStandEndedActivation = false;
	armorerLastStandDefending = false;
	battlecraftWaitMasteryDoubled = false;
	battlecraftDefendMasteryDoubled = false;
	// Keep the remains ledger on a ghost until the battle result is captured.
	// Ghost stacks can be removed from the battlefield before the final result
	// is assembled; clearing the ledger here would make those direct-hit
	// casualties look like ordinary Necromancy-eligible deaths.
	health.reset(false);
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	ghostPending = false;
	ghost = true;
}
}
