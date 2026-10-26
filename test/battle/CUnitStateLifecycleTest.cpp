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
#include <type_traits>
#include <utility>

namespace test
{
using namespace ::testing;

// These assertions must be compiled on the unchanged implementation first.
static_assert(!std::is_copy_constructible_v<battle::CUnitState>);
static_assert(!std::is_move_constructible_v<battle::CUnitState>);
static_assert(std::is_copy_assignable_v<battle::CUnitState>);
static_assert(!std::is_move_assignable_v<battle::CUnitState>);
static_assert(!std::is_copy_constructible_v<battle::CUnitStateDetached>);
static_assert(!std::is_move_constructible_v<battle::CUnitStateDetached>);
static_assert(std::is_copy_assignable_v<battle::CUnitStateDetached>);
static_assert(std::is_move_assignable_v<battle::CUnitStateDetached>);

namespace
{
class DelegationBearer final : public BonusBearerMock
{
public:
	mutable std::string key;
	mutable int stacked = 0, unstacked = 0;
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cacheKey = "") const override
	{
		key = cacheKey;
		++stacked;
		return BonusBearerMock::getAllBonuses(selector, cacheKey);
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		++unstacked;
		return BonusBearerMock::getUnstackedBonuses(selector);
	}
};
}

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

TEST_F(UnitLifecycleTest, genericProviderDelegatesKeyAndRawModeAndObservesLaterMembership)
{
	DelegationBearer provider;
	battle::CUnitStateDetached view(&info, &provider);
	EXPECT_EQ(provider.stacked, 0);
	EXPECT_EQ(provider.unstacked, 0);
	EXPECT_EQ(view.getBonusBearer(), &view);
	const auto marker = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, 2, BonusSourceID());
	provider.addNewBonus(marker);
	const auto selector = Selector::type()(BonusType::LUCK);
	const auto found = view.getBonusesBeforeCreatureAbilitySuppression(selector, "detached.characterization");
	ASSERT_EQ(found->size(), 1u);
	EXPECT_EQ(found->front(), marker);
	EXPECT_EQ(provider.key, "detached.characterization");
	EXPECT_EQ(provider.stacked, 1);
	EXPECT_EQ(provider.unstacked, 0);
	const auto raw = view.getBonusesBeforeCreatureAbilitySuppression(selector, "ignored.raw.key", true);
	ASSERT_EQ(raw->size(), 1u);
	EXPECT_EQ(raw->front(), marker);
	EXPECT_EQ(provider.unstacked, 1);
	provider.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, 3, BonusSourceID()));
	EXPECT_EQ(view.valOfBonuses(BonusType::LUCK), 5);
	EXPECT_EQ(view.getTreeVersion(), provider.getTreeVersion());
}

TEST_F(UnitLifecycleTest, bothAcquireEntrypointsRetainProviderChainsAndIndependentState)
{
	seed();
	const auto before = unit.save();
	const auto asUnit = unit.acquire();
	const auto asState = unit.acquireState();
	const auto nested = asState->acquireState();
	for(auto * acquired : std::array<battle::CUnitState *, 3>{
		dynamic_cast<battle::CUnitState *>(asUnit.get()), asState.get(), nested.get()})
	{
		ASSERT_NE(acquired, nullptr);
		EXPECT_EQ(acquired->save(), before);
		EXPECT_EQ(acquired->valOfBonuses(BonusType::LUCK), 0);
	}
	add(BonusType::LUCK, 4);
	EXPECT_EQ(asUnit->valOfBonuses(BonusType::LUCK), 4);
	EXPECT_EQ(asState->valOfBonuses(BonusType::LUCK), 4);
	EXPECT_EQ(nested->valOfBonuses(BonusType::LUCK), 4);
	asState->afterNewRound();
	EXPECT_EQ(unit.save(), before);
	EXPECT_EQ(nested->save(), before);
}

TEST_F(UnitLifecycleTest, sameTypeAssignmentRebindsProviderWhileBaseAssignmentKeepsIt)
{
	BonusBearerMock otherBonuses;
	otherBonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, 9, BonusSourceID()));
	UnitInfoMock otherInfo;
	EXPECT_CALL(otherInfo, unitBaseAmount()).WillRepeatedly(Return(3));
	EXPECT_CALL(otherInfo, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
	battle::CUnitStateDetached other(&otherInfo, &otherBonuses);
	other.localInit(&environment);
	other.defending = true;
	const auto sourceBefore = other.save();
	const auto acquired = unit.acquireState();
	auto * destination = dynamic_cast<battle::CUnitStateDetached *>(acquired.get());
	ASSERT_NE(destination, nullptr);
	*destination = static_cast<const battle::CUnitState &>(other);
	EXPECT_TRUE(destination->usesBonusProvider(unit));
	EXPECT_TRUE(destination->usesUnitInfo(unit));
	EXPECT_EQ(destination->valOfBonuses(BonusType::LUCK), 0);
	*destination = other;
	EXPECT_TRUE(destination->usesBonusProvider(otherBonuses));
	EXPECT_TRUE(destination->usesUnitInfo(otherInfo));
	EXPECT_EQ(destination->valOfBonuses(BonusType::LUCK), 9);
	EXPECT_EQ(destination->save(), sourceBefore);
	*destination = *destination;
	EXPECT_EQ(destination->save(), sourceBefore);
	// Rvalue assignment's availability and actual behavior are characterized together.
	*destination = std::move(other);
	EXPECT_TRUE(destination->usesBonusProvider(otherBonuses));
	EXPECT_EQ(other.save(), sourceBefore);
	const auto admitted = unit.acquireState();
	*destination = dynamic_cast<const battle::CUnitStateDetached &>(*admitted);
	EXPECT_TRUE(destination->usesBonusProvider(unit));
	add(BonusType::LUCK, 5);
	EXPECT_EQ(destination->valOfBonuses(BonusType::LUCK), 5);

	// Distinct providers have the SAME dynamic type: a type mismatch cannot
	// rescue a stale dispatch pointer when either assignment forgets to pair it.
	const auto first = unit.acquireState();
	const auto second = other.acquireState();
	const auto originalFirst = unit.acquireState();
	auto & a = dynamic_cast<battle::CUnitStateDetached &>(*first);
	auto & b = dynamic_cast<battle::CUnitStateDetached &>(*second);
	const auto & savedA = dynamic_cast<const battle::CUnitStateDetached &>(*originalFirst);
	a = b;
	EXPECT_TRUE(a.usesBonusProvider(other));
	EXPECT_EQ(a.valOfBonuses(BonusType::LUCK), 9);
	const auto aRaw = a.getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::LUCK), {}, true);
	ASSERT_EQ(aRaw->size(), 1u);
	EXPECT_EQ(aRaw->front()->val, 9);
	b = savedA;
	EXPECT_TRUE(b.usesBonusProvider(unit));
	EXPECT_EQ(b.valOfBonuses(BonusType::LUCK), 5);
	const auto bRaw = b.getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::LUCK), {}, true);
	ASSERT_EQ(bRaw->size(), 1u);
	EXPECT_EQ(bRaw->front()->val, 5);
}
}
