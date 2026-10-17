/*
 * ProjectedUnitTransitionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/UnitTransitionSidecars.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusParameters.h"
#include <limits>
#include <stdexcept>

namespace
{
using Arena = battle::ProjectedBattleWorldArena;
class ProjectedUnitTransitionTest : public BattleTestFixture
{
protected:
	std::unique_ptr<Arena> arena;
	CStack * subject = nullptr;
	CStack * neighbor = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
		subject = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
		neighbor = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(rightHex), 4);
	}
	void TearDown() override
	{
		arena.reset();
		BattleTestFixture::TearDown();
	}
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	size_t subjectIndex() const { return battle()->stacks.size() - 2; }
	JsonNode damagedState(int64_t amount)
	{
		const auto initial = subject->save();
		subject->damage(amount);
		const auto requested = subject->save();
		subject->load(initial);
		EXPECT_EQ(subject->save(), initial);
		return requested;
	}
	void protect()
	{
		HeroOrderState state;
		state.command = HeroCommand::PROTECT;
		state.issuedRound = 1;
		state.primaryTargetUnitId = subject->unitId();
		state.secondaryTargetUnitId = neighbor->unitId();
		battle()->setHeroOrderStates(BattleSide::ATTACKER, {state});
	}
	void reduced(uint32_t id)
	{
		ReducedExtraActivationState state;
		state.enabled = state.used = true;
		state.activeUnitId = id;
		state.outputPercent = 50;
		battle()->setReducedExtraActivationState(BattleSide::ATTACKER, state);
	}
	void scalarParity(const JsonNode & data, int64_t delta)
	{
		const auto initial = subject->save();
		capture();
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		ASSERT_EQ(arena->updateUnit(subject->unitId(), data, delta), Arena::UpdateDecision::APPLY);
		const auto expected = arena->snapshotAt(subjectIndex());
		const auto expectedOrders = arena->orderStates(BattleSide::ATTACKER);
		const auto expectedReduced = arena->reducedActivation(BattleSide::ATTACKER);
		EXPECT_EQ(subject->save(), initial);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		arena.reset();
		battle()->updateUnit(subject->unitId(), data, delta);
		EXPECT_EQ(subject->save(), expected);
		EXPECT_EQ(battle()->getReducedExtraActivationState(BattleSide::ATTACKER), expectedReduced);
		EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), expectedOrders);
		EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER).size(), expectedOrders.size());
		if(!expectedOrders.empty())
		{
			EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER).front().protectBroken, expectedOrders.front().protectBroken);
		}
	}
};

TEST_F(ProjectedUnitTransitionTest, OrdinaryZeroDeltaUpdateMatchesWholeJsonAndOriginalRemainsUntouched)
{
	auto data = subject->save();
	data["state"]["defending"].Bool() = true;
	scalarParity(data, 0);
}

TEST_F(ProjectedUnitTransitionTest, NegativeHealthUsesOriginalDamageAndUntilAttackedCleanup)
{
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_BEING_ATTACKED,
		BonusType::LUCK, BonusSource::OTHER, 2, BonusSourceID()));
	const auto data = damagedState(1);
	capture();
	ASSERT_EQ(arena->updateUnit(subject->unitId(), data, -1), Arena::UpdateDecision::APPLY);
	EXPECT_EQ(arena->unitAt(subjectIndex()).getAvailableHealth(), subject->getAvailableHealth() - 1);
	EXPECT_FALSE(arena->unitAt(subjectIndex()).hasBonus(Bonus::UntilBeingAttacked));
	EXPECT_TRUE(subject->hasBonus(Bonus::UntilBeingAttacked));
}

TEST_F(ProjectedUnitTransitionTest, NegativeHealthWholeJsonMatchesScalar)
{
	const auto data = damagedState(1);
	scalarParity(data, -1);
}

TEST_F(ProjectedUnitTransitionTest, ResurrectionWholeJsonMatchesScalarAndUsesDeathCleanupBoundary)
{
	const auto healthy = subject->save();
	int64_t damage = subject->getAvailableHealth();
	subject->damage(damage);
	ASSERT_FALSE(subject->alive());
	scalarParity(healthy, 1);
	EXPECT_TRUE(subject->alive());
}

TEST_F(ProjectedUnitTransitionTest, DeathRemovesTransientSpellButRetainsPersistentDisruptingRay)
{
	for(const auto spell : {SpellID::HASTE, SpellID::DISRUPTING_RAY})
		subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(spell))));
	const auto damage = subject->getAvailableHealth();
	const auto data = damagedState(damage);
	capture();
	arena->updateUnit(subject->unitId(), data, -damage);
	const auto & projected = arena->unitAt(subjectIndex());
	EXPECT_FALSE(projected.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_TRUE(projected.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::DISRUPTING_RAY)))));
	EXPECT_TRUE(subject->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
}

TEST_F(ProjectedUnitTransitionTest, ProtectExpiresAfterLoadedPositionAndNeverRearmsWhenAdjacentAgain)
{
	protect();
	auto moved = subject->save();
	moved["state"]["position"].Integer() = leftHex - 4;
	const auto initial = subject->save();
	capture();
	arena->updateUnit(subject->unitId(), moved, 0);
	ASSERT_EQ(arena->orderStates(BattleSide::ATTACKER).size(), 1);
	EXPECT_TRUE(arena->orderStates(BattleSide::ATTACKER).front().protectBroken);
	arena->updateUnit(subject->unitId(), initial, 0);
	EXPECT_TRUE(arena->orderStates(BattleSide::ATTACKER).front().protectBroken);
	EXPECT_FALSE(battle()->getHeroOrderStates(BattleSide::ATTACKER).front().protectBroken);
}

TEST_F(ProjectedUnitTransitionTest, ProtectExpiryMatchesScalarPositionUpdate)
{
	protect();
	auto moved = subject->save();
	moved["state"]["position"].Integer() = leftHex - 4;
	scalarParity(moved, 0);
}

TEST_F(ProjectedUnitTransitionTest, DeadUpdateClearsOnlyMatchingActiveRecipientWithoutResettingAllowance)
{
	reduced(subject->unitId());
	const auto damage = subject->getAvailableHealth();
	scalarParity(damagedState(damage), -damage);
	const auto & state = battle()->getReducedExtraActivationState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.enabled && state.used);
	EXPECT_FALSE(state.hasActiveUnit());
	EXPECT_EQ(state.outputPercent, 100);
}

TEST_F(ProjectedUnitTransitionTest, DeadUpdateDoesNotClearAnotherActiveRecipient)
{
	reduced(neighbor->unitId());
	const auto damage = subject->getAvailableHealth();
	scalarParity(damagedState(damage), -damage);
	EXPECT_EQ(battle()->getReducedExtraActivationState(BattleSide::ATTACKER).activeUnitId, neighbor->unitId());
}

TEST_F(ProjectedUnitTransitionTest, KilledCloneBacklinksAndCloneGhostTransitionMatchScalar)
{
	subject->cloneID = neighbor->unitId();
	neighbor->cloned = true;
	const auto damage = subject->getAvailableHealth();
	const auto data = damagedState(damage);
	capture();
	arena->updateUnit(subject->unitId(), data, -damage);
	const auto expected = arena->snapshotAt(subjectIndex() + 1);
	EXPECT_TRUE(arena->unitAt(subjectIndex() + 1).ghostPending);
	EXPECT_FALSE(neighbor->ghostPending);
	arena.reset();
	battle()->updateUnit(subject->unitId(), data, -damage);
	EXPECT_EQ(neighbor->save(), expected);
	EXPECT_EQ(subject->cloneID, -1);
}

TEST_F(ProjectedUnitTransitionTest, DeadCloneClearsEveryBacklinkButDoesNotMutateLiveOriginals)
{
	subject->cloned = true;
	neighbor->cloneID = subject->unitId();
	const auto damage = subject->getAvailableHealth();
	const auto data = damagedState(damage);
	capture();
	arena->updateUnit(subject->unitId(), data, -damage);
	EXPECT_EQ(arena->findUnit(neighbor->unitId())->cloneID, -1);
	EXPECT_EQ(neighbor->cloneID, subject->unitId());
}

TEST_F(ProjectedUnitTransitionTest, RepeatedIdIntermediateFormLoadsUseSynchronousExactProvider)
{
	const auto base = subject->save();
	subject->beginBattleForm(CreatureID(2), 3);
	const auto form = subject->save();
	subject->endBattleForm();
	subject->load(base);
	ASSERT_EQ(subject->save(), base);
	capture();
	arena->updateUnit(subject->unitId(), form, 0);
	ASSERT_TRUE(arena->unitAt(subjectIndex()).hasBattleForm());
	arena->updateUnit(subject->unitId(), base, 0);
	EXPECT_FALSE(arena->unitAt(subjectIndex()).hasBattleForm());
	arena->updateUnit(subject->unitId(), form, 0);
	EXPECT_EQ(arena->snapshotAt(subjectIndex()), form);
	EXPECT_EQ(subject->save(), base);
}

TEST_F(ProjectedUnitTransitionTest, MalformedLaterStateDiscardsPrivatePrefixWithoutChangingLiveValues)
{
	const auto before = subject->save();
	auto first = before;
	first["state"]["defending"].Bool() = true;
	auto malformed = before;
	malformed["state"]["guardianSpiritHitPoints"].Integer() = -1;
	capture();
	arena->updateUnit(subject->unitId(), first, 0);
	EXPECT_TRUE(arena->unitAt(subjectIndex()).defending);
	EXPECT_THROW(arena->updateUnit(subject->unitId(), malformed, 0), std::runtime_error);
	EXPECT_THROW(arena->snapshotAt(subjectIndex()), std::logic_error);
	EXPECT_EQ(subject->save(), before);
}

TEST_F(ProjectedUnitTransitionTest, TimeStopIgnoresMalformedAndMinimumDeltaBeforeParsingOrPrivateGuard)
{
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	capture();
	const JsonNode malformed("not a state object");
	EXPECT_EQ(arena->updateUnit(subject->unitId(), malformed, std::numeric_limits<int64_t>::min()), Arena::UpdateDecision::IGNORE_TIME_STOP);
	EXPECT_EQ(arena->updateUnit(subject->unitId(), malformed, 0), Arena::UpdateDecision::IGNORE_TIME_STOP);
	EXPECT_EQ(arena->size(), battle()->stacks.size());
}

TEST_F(ProjectedUnitTransitionTest, UnignoredMinimumDeltaRefusesBeforeUndefinedNegationOrLoad)
{
	const auto before = subject->save();
	capture();
	try
	{
		arena->updateUnit(subject->unitId(), JsonNode("not a state"), std::numeric_limits<int64_t>::min());
		FAIL() << "Expected exact private invalid-domain refusal";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Projected unit update health delta cannot be negated");
	}
	EXPECT_THROW(arena->size(), std::logic_error);
	EXPECT_EQ(subject->save(), before);
}

TEST_F(ProjectedUnitTransitionTest, OccupiedResurrectionIgnoresMalformedPayloadBeforeLoad)
{
	int64_t damage = subject->getAvailableHealth();
	subject->damage(damage);
	addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
	capture();
	EXPECT_EQ(arena->updateUnit(subject->unitId(), JsonNode("malformed"), 1), Arena::UpdateDecision::IGNORE_OCCUPIED_RESURRECTION);
	EXPECT_EQ(arena->size(), battle()->stacks.size());
}

TEST_F(ProjectedUnitTransitionTest, CompletedRemoveClearsRootActivationAndExpiresProtect)
{
	protect();
	reduced(subject->unitId());
	capture();
	ASSERT_TRUE(arena->removeUnit(subject->unitId()).completed);
	EXPECT_TRUE(arena->findUnit(subject->unitId())->isGhost());
	EXPECT_TRUE(arena->orderStates(BattleSide::ATTACKER).front().protectBroken);
	EXPECT_FALSE(arena->reducedActivation(BattleSide::ATTACKER).hasActiveUnit());
	EXPECT_TRUE(battle()->getReducedExtraActivationState(BattleSide::ATTACKER).hasActiveUnit());
}

TEST_F(ProjectedUnitTransitionTest, MissingCloneReturnsAfterPrivateRemovalPrefixBeforeBothCompletionSidecars)
{
	protect();
	reduced(subject->unitId());
	subject->cloneID = 99999;
	capture();
	const auto result = arena->removeUnit(subject->unitId());
	EXPECT_FALSE(result.completed);
	EXPECT_EQ(result.missingId, 99999u);
	EXPECT_TRUE(arena->findUnit(subject->unitId())->isGhost());
	EXPECT_FALSE(arena->orderStates(BattleSide::ATTACKER).front().protectBroken);
	EXPECT_TRUE(arena->reducedActivation(BattleSide::ATTACKER).hasActiveUnit());
	EXPECT_TRUE(subject->alive());
}

TEST_F(ProjectedUnitTransitionTest, RecursiveCloneRemovalDoesNotClearItsActivationWhenOnlyRootCompletes)
{
	reduced(neighbor->unitId());
	subject->cloneID = neighbor->unitId();
	neighbor->cloned = true;
	capture();
	ASSERT_TRUE(arena->removeUnit(subject->unitId()).completed);
	EXPECT_TRUE(arena->findUnit(neighbor->unitId())->isGhost());
	EXPECT_EQ(arena->reducedActivation(BattleSide::ATTACKER).activeUnitId, neighbor->unitId());
}

TEST_F(ProjectedUnitTransitionTest, CompletedRemoveWholeValuesAndSidecarsMatchScalarAfterRetirement)
{
	protect();
	reduced(subject->unitId());
	capture();
	ASSERT_TRUE(arena->removeUnit(subject->unitId()).completed);
	const auto expected = arena->snapshotAt(subjectIndex());
	const auto active = arena->reducedActivation(BattleSide::ATTACKER);
	const auto broken = arena->orderStates(BattleSide::ATTACKER).front().protectBroken;
	arena.reset();
	battle()->removeUnit(subject->unitId());
	EXPECT_EQ(subject->save(), expected);
	EXPECT_EQ(battle()->getReducedExtraActivationState(BattleSide::ATTACKER), active);
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER).front().protectBroken, broken);
}
}
