/*
 * StackInitializationReceipts.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "CUnitState.h"
#include <tuple>

namespace battle
{
/// Exactly the legacy localInit receipts, NOT a second unit-state registry.
/// Capture before graph initialization; initialize only after graph/environment
/// binding. reset/health may throw: the caller must abandon its private world.
/// No graph edit, catch/fallback, summon restoration or packet validation here.
class DLL_LINKAGE StackInitializationReceipts final
{
	int32_t restoredPersonalBloodrageIncrement;
	std::tuple<bool, bool, bool, bool, bool, bool, bool> restoredQueueFlags;
	bool restoredVeteranCohesion;
	ConfusionState restoredConfusionState;
	int32_t restoredOverwatchReadyRound;
	int32_t restoredOverwatchUsedRound;
	bool restoredMoraleExtraActivation;
	bool restoredHeroicSpirit;
	bool restoredHeroicSpiritPending;
	int32_t restoredPostAttackMovement;
	bool restoredLuckyOwnAttackSequence;
	int32_t restoredFrozenAppliedRound;
	bool restoredTimeStopTurnConsumed;
	int64_t restoredVeteranDamageHistory;
	int32_t restoredNoQuarterLifetime;
public:
	explicit StackInitializationReceipts(const CUnitState & state);
	void initialize(CUnitState & state, const IUnitEnvironment * environment, const BattleHex & initialPosition) const;
};
}
