/*
 * StackInitializationReceipts.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "StackInitializationReceipts.h"

battle::StackInitializationReceipts::StackInitializationReceipts(const CUnitState & state)
{
	restoredPersonalBloodrageIncrement = state.personalBloodrageIncrement;
	restoredQueueFlags = std::make_tuple(state.waiting, state.waitedThisTurn, state.movedThisRound,
		state.defending, state.hadMorale, state.battlecraftWaitBonusUsed, state.battlecraftWaitMasteryDoubled);
	restoredVeteranCohesion = state.veteranCohesionEarned;
	restoredConfusionState = state.confusionState;
	restoredForestAmbush = state.forestAmbush;
	restoredDreadCavalry = state.dreadCavalryState;
	restoredOverwatchReadyRound = state.battlecraftOverwatchReadyRound;
	restoredPetrifyingBloodRound = state.petrifyingBloodRound;
	restoredOverwatchUsedRound = state.battlecraftOverwatchUsedRound;
	restoredMoraleExtraActivation = state.moraleExtraActivation;
	restoredHeroicSpirit = state.heroicSpiritRetaliation;
	restoredHeroicSpiritPending = state.heroicSpiritMoralePending;
	restoredPostAttackMovement = state.pursuitMovementRemaining;
	restoredLuckyOwnAttackSequence = state.luckyOwnAttackSequence;
	restoredFrozenAppliedRound = state.frozenLastAppliedRound();
	restoredTimeStopTurnConsumed = state.timeStopTurnConsumedFlag;
	restoredVeteranDamageHistory = state.veteranPhysicalDamageSinceActivation;
	restoredNoQuarterLifetime = state.noQuarterMoraleActivationsRemaining;

}

void battle::StackInitializationReceipts::initialize(CUnitState & state,
	const IUnitEnvironment * environment, const BattleHex & initialPosition) const
{
	state.localInit(environment); //it causes execution of the CStack::isOnNativeTerrain where nativeTerrain will be considered
	// CUnitState::localInit resets ordinary per-battle transient state; preserve
	// the one personal increment explicitly carried by this binary stack snapshot.
	std::tie(state.waiting, state.waitedThisTurn, state.movedThisRound, state.defending, state.hadMorale,
		state.battlecraftWaitBonusUsed, state.battlecraftWaitMasteryDoubled) = restoredQueueFlags;
	state.personalBloodrageIncrement = restoredPersonalBloodrageIncrement;
	state.veteranCohesionEarned = restoredVeteranCohesion;
	// Binary stack descriptors carry pending Confusion and target history too;
	// rebinding the stack to the battle must not consume either value.
	state.confusionState = restoredConfusionState;
	state.forestAmbush = restoredForestAmbush;
	state.dreadCavalryState = restoredDreadCavalry;
	state.battlecraftOverwatchReadyRound = restoredOverwatchReadyRound;
	state.petrifyingBloodRound = restoredPetrifyingBloodRound;
	state.battlecraftOverwatchUsedRound = restoredOverwatchUsedRound;
	state.moraleExtraActivation = restoredMoraleExtraActivation;
	state.heroicSpiritRetaliation = restoredHeroicSpirit;
	state.heroicSpiritMoralePending = restoredHeroicSpiritPending;
	state.restoreFrozenApplicationRound(restoredFrozenAppliedRound);
	state.timeStopTurnConsumedFlag = restoredTimeStopTurnConsumed;
	state.veteranPhysicalDamageSinceActivation = restoredVeteranDamageHistory;
	state.noQuarterMoraleActivationsRemaining = restoredNoQuarterLifetime;
	state.luckyOwnAttackSequence = restoredLuckyOwnAttackSequence;
	state.pursuitMovementRemaining = restoredPostAttackMovement;
	state.position = initialPosition;
}
