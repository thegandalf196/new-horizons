/*
 * CUnitStateLifecycleTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/json/JsonNode.h"

#include <array>

namespace test
{
using namespace ::testing;

// Reuse the maintained UnitStateTest mocks and detached-unit initialization.
class UnitLifecycleTest : public Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses;
	battle::CUnitStateDetached unit{&info, &bonuses};

	void SetUp() override
	{
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(10));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		add(BonusType::STACK_HEALTH, 20);
		add(BonusType::STACKS_SPEED, 7);
		add(BonusType::SHOOTER, 1);
		add(BonusType::SHOTS, 5);
		add(BonusType::CASTS, 3);
		unit.localInit(&environment);
		unit.position = BattleHex(5, 5);
	}

	void add(BonusType type, int value)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			type, BonusSource::CREATURE_ABILITY, value, BonusSourceID()));
	}

	void seed()
	{
		unit.defending = unit.castSpellThisTurn = unit.movedThisRound = true;
		unit.hadMorale = unit.moraleExtraActivation = true;
		unit.waiting = unit.waitedThisTurn = unit.battlecraftWaitBonusUsed = true;
		unit.battlecraftWaitMasteryDoubled = unit.battlecraftDefendMasteryDoubled = true;
		unit.defensiveStanceMeleeBonus = 4;
		unit.defensiveStanceRangedBonus = 6;
		unit.battlecraftOverwatchReadyRound = 3;
		unit.battlecraftOverwatchUsedRound = 2;
		unit.battlecraftPreemptiveStrikeRound = 3;
		unit.archeryCounterfireRound = unit.archeryDeadeyeRound = 3;
		unit.archeryCrossfireRound = 3;
		unit.archeryCrossfireAttackers = {11, 13};
		unit.archeryCrossfireDefenders = {17};
		unit.noQuarterMoraleActivationsRemaining = 2;
		unit.veteranCohesionEarned = true;
		unit.veteranPhysicalDamageSinceActivation = 9;
		unit.pursuitMovementRemaining = 2;
		unit.cleaveUsedThisActivation = unit.luckyOwnAttackSequence = true;
		unit.rangedFollowUpDamagePercent = 50;
		unit.setActivationMovementBonus(3);
		unit.confusionState.previousResolved = battle::ConfusionBehavior::WANDER;
		unit.confusionState.applyPending(PlayerColor(0), true);
		unit.shots.use();
		unit.casts.use();
		unit.counterAttacks.use();
	}

	JsonNode canonical(const JsonNode & expected)
	{
		battle::CUnitStateDetached restored(&info, &bonuses);
		restored.localInit(&environment);
		restored.load(expected);
		return restored.save();
	}
};

TEST_F(UnitLifecycleTest, directTurnHookCharacterizesAllNormalForcedExtraAndContinuationReasons)
{
	constexpr std::array reasons{BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE,
		BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::AUTOMATIC_ACTION, BattleUnitTurnReason::HERO_COMMAND,
		BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION,
		BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION, BattleUnitTurnReason::CRISIS_ORDER,
		BattleUnitTurnReason::CRISIS_RESUME};
	for(const auto reason : reasons)
	{
		SCOPED_TRACE(static_cast<int>(reason));
		unit.localInit(&environment);
		seed();
		unit.hadMorale = unit.moraleExtraActivation = false;
		auto expected = unit.save();
		auto & state = expected["state"];
		state["battlecraftOverwatchReadyRound"].Integer() = -1;
		state["defending"].Bool() = false;
		state["defensiveStanceMeleeBonus"].Integer() = 0;
		state["defensiveStanceRangedBonus"].Integer() = 0;
		state["battlecraftDefendMasteryDoubled"].Bool() = false;
		if(reason == BattleUnitTurnReason::MORALE || reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION)
		{
			state["castSpellThisTurn"].Bool() = state["moved"].Bool() = false;
			if(reason == BattleUnitTurnReason::MORALE)
				state["hadMorale"].Bool() = true;
			if(reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION)
				state["waiting"].Bool() = false;
		}
		unit.afterGetsTurn(reason);
		EXPECT_EQ(unit.save(), canonical(expected));
		EXPECT_EQ(unit.getActivationMovementBonus(), 3);
	}
}

TEST_F(UnitLifecycleTest, newRoundClearsRoundFieldsButPreservesCapturedStanceHistoryAndPendingConfusion)
{
	seed();
	auto expected = unit.save();
	auto & state = expected["state"];
	for(const auto * name : {"moraleExtraActivation", "defending", "bulwarkPreemptiveUsed", "waiting",
		"waitedThisTurn", "battlecraftWaitBonusUsed", "battlecraftWaitMasteryDoubled", "timeStopTurnConsumed",
		"moved", "cleaveUsedThisActivation", "luckyOwnAttackSequence", "hadMorale", "castSpellThisTurn",
		"fear", "drainedMana"})
		state[name].Bool() = false;
	for(const auto * name : {"activationMovementBonus", "pursuitMovementRemaining", "rangedFollowUpDamagePercent"})
		state[name].Integer() = 0;
	for(const auto * name : {"battlecraftOverwatchReadyRound", "battlecraftOverwatchUsedRound", "archeryCounterfireRound"})
		state[name].Integer() = -1;
	state["counterAttacks"]["used"].Integer() = 0;
	state["counterAttacks"]["totalCache"].Integer() = 0;
	unit.afterNewRound();
	EXPECT_EQ(unit.save(), canonical(expected));
	EXPECT_EQ(unit.shots.available(), 4);
	EXPECT_EQ(unit.casts.available(), 2);
	EXPECT_EQ(unit.counterAttacks.available(), 1);
}

TEST_F(UnitLifecycleTest, waitAndAttackKeepPhysicalConsumptionAndAmmunitionOrder)
{
	unit.afterWait();
	unit.battlecraftWaitMasteryDoubled = true;
	unit.afterAttack(false, false, false);
	EXPECT_FALSE(unit.battlecraftWaitBonusUsed);
	EXPECT_TRUE(unit.battlecraftWaitMasteryDoubled);
	unit.afterAttack(true, true, true);
	EXPECT_EQ(unit.shots.available(), 4);
	EXPECT_EQ(unit.counterAttacks.available(), 0);
	EXPECT_TRUE(unit.battlecraftWaitBonusUsed);
	EXPECT_FALSE(unit.battlecraftWaitMasteryDoubled);
	unit.afterWait();
	EXPECT_TRUE(unit.battlecraftWaitBonusUsed);
	EXPECT_EQ(unit.shots.available(), 4);
	EXPECT_TRUE(unit.waiting);
	EXPECT_TRUE(unit.waitedThisTurn);
}

TEST_F(UnitLifecycleTest, detachedCopyAndSavedRoundRetainIndependentReceiptsAndDestinationAmmoEnvironment)
{
	seed();
	const auto saved = unit.save();
	UnitEnvironmentMock otherEnvironment;
	EXPECT_CALL(otherEnvironment, unitHasAmmoCart(_)).WillRepeatedly(Return(true));
	battle::CUnitStateDetached copy(&info, &bonuses);
	copy.localInit(&otherEnvironment);
	copy = unit;
	EXPECT_EQ(copy.save(), saved);
	copy.shots.use(); // Destination ammo cart makes this unlimited; source remains limited.
	EXPECT_EQ(copy.shots.available(), 4);
	EXPECT_EQ(unit.shots.available(), 4);
	copy.afterNewRound();
	EXPECT_EQ(unit.save(), saved);
	battle::CUnitStateDetached restored(&info, &bonuses);
	restored.localInit(&environment);
	restored.load(saved);
	EXPECT_EQ(restored.save(), saved);
	restored.afterNewRound();
	EXPECT_EQ(restored.save(), copy.save());
}

TEST_F(UnitLifecycleTest, reinitializationRetainsCurrentPursuitCleaveBehaviorWithoutInventingReset)
{
	seed();
	unit.localInit(&environment);
	battle::CUnitStateDetached fresh(&info, &bonuses);
	fresh.localInit(&environment);
	// Existing reset omits these two fields; preserve this BEFORE characterization.
	fresh.pursuitMovementRemaining = 2;
	fresh.cleaveUsedThisActivation = true;
	EXPECT_EQ(unit.save(), fresh.save());
	EXPECT_EQ(unit.getAvailableHealth(), 200);
	EXPECT_EQ(unit.shots.available(), 5);
	EXPECT_EQ(unit.casts.available(), 3);
	EXPECT_FALSE(unit.confusionState.hasState());
}

TEST_F(UnitLifecycleTest, removalPreservesDestroyedRemainsWhileClearingTransientDurability)
{
	int64_t damage = 40;
	unit.damage(damage, true);
	ASSERT_EQ(unit.getUnusableRemains(), 2);
	unit.guardianSpiritHitPoints = 17;
	unit.guardianSpiritRoundsRemaining = 2;
	unit.onRemoved();
	EXPECT_TRUE(unit.ghost);
	EXPECT_FALSE(unit.ghostPending);
	EXPECT_EQ(unit.getAvailableHealth(), 0);
	EXPECT_EQ(unit.getUnusableRemains(), 2);
	EXPECT_EQ(unit.getGuardianSpiritHitPoints(), 0);
	EXPECT_EQ(unit.getGuardianSpiritRoundsRemaining(), 0);
}

TEST_F(UnitLifecycleTest, ghostTransitionSeparatesPendingFromActualRemoval)
{
	seed();
	unit.makeGhost();
	EXPECT_TRUE(unit.ghostPending);
	EXPECT_FALSE(unit.ghost);
	EXPECT_FALSE(unit.moraleExtraActivation);
	EXPECT_EQ(unit.pursuitMovementRemaining, 0);
	EXPECT_FALSE(unit.cleaveUsedThisActivation);
	EXPECT_EQ(unit.getActivationMovementBonus(), 0);
	EXPECT_EQ(unit.getAvailableHealth(), 0);
	unit.onRemoved();
	EXPECT_TRUE(unit.ghost);
	EXPECT_FALSE(unit.ghostPending);
}

TEST_F(UnitLifecycleTest, roundGhostsUnmarkedCloneButRetainsMarkedClone)
{
	unit.cloned = true;
	unit.afterNewRound();
	EXPECT_TRUE(unit.ghostPending);
	unit.localInit(&environment);
	unit.cloned = true;
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NONE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::CLONE))));
	unit.afterNewRound();
	EXPECT_FALSE(unit.ghostPending);
	EXPECT_TRUE(unit.alive());
}
}
