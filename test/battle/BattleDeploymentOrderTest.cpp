/*
 * BattleDeploymentOrderTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../../lib/battle/BattleDeploymentState.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
BattleDeploymentState twoSidedDeployment(BattleSide firstSide)
{
	BattleDeploymentState state;
	state.independent = true;
	state.initialFirstSide = firstSide;
	state.distances[BattleSide::ATTACKER] = 3;
	state.distances[BattleSide::DEFENDER] = 3;
	return state;
}
}

TEST(BattleDeploymentOrderTest, InitialOpportunitiesFollowResolvedFirstSide)
{
	auto state = twoSidedDeployment(BattleSide::DEFENDER);
	EXPECT_EQ(state.initialActiveSide(), BattleSide::DEFENDER);
	EXPECT_EQ(state.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(state.activeDistance(), 3);
	EXPECT_FALSE(state.isFinalRelocation());

	state.complete(BattleSide::DEFENDER);
	EXPECT_EQ(state.activeSide(), BattleSide::ATTACKER);
	state.complete(BattleSide::ATTACKER);
	EXPECT_EQ(state.activeSide(), BattleSide::NONE);
	EXPECT_FALSE(state.isFinalRelocation());
}

TEST(BattleDeploymentOrderTest, SoleInitialOpportunityWinsRegardlessOfConfiguredFirstSide)
{
	auto attackerOnly = twoSidedDeployment(BattleSide::DEFENDER);
	attackerOnly.distances[BattleSide::DEFENDER] = 0;
	EXPECT_EQ(attackerOnly.activeSide(), BattleSide::ATTACKER);
	attackerOnly.complete(BattleSide::ATTACKER);
	EXPECT_EQ(attackerOnly.activeSide(), BattleSide::NONE);

	auto defenderOnly = twoSidedDeployment(BattleSide::ATTACKER);
	defenderOnly.distances[BattleSide::ATTACKER] = 0;
	EXPECT_EQ(defenderOnly.activeSide(), BattleSide::DEFENDER);
	defenderOnly.complete(BattleSide::DEFENDER);
	EXPECT_EQ(defenderOnly.activeSide(), BattleSide::NONE);
}

TEST(BattleDeploymentOrderTest, FinalRelocationRemainsAttackerThenDefenderAfterBothInitialPhases)
{
	auto state = twoSidedDeployment(BattleSide::DEFENDER);
	state.finalRelocationDistances[BattleSide::ATTACKER] = 1;
	state.finalRelocationDistances[BattleSide::DEFENDER] = 1;

	EXPECT_EQ(state.activeSide(), BattleSide::DEFENDER);
	EXPECT_FALSE(state.isFinalRelocation());
	state.complete(BattleSide::DEFENDER);
	EXPECT_EQ(state.activeSide(), BattleSide::ATTACKER);
	EXPECT_FALSE(state.isFinalRelocation());
	state.complete(BattleSide::ATTACKER);

	EXPECT_EQ(state.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(state.activeDistance(), 1);
	EXPECT_TRUE(state.isFinalRelocation());
	EXPECT_THROW(state.complete(BattleSide::DEFENDER), std::runtime_error);
	EXPECT_EQ(state.activeSide(), BattleSide::ATTACKER);
	state.complete(BattleSide::ATTACKER);
	EXPECT_EQ(state.activeSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(state.isFinalRelocation());
	state.complete(BattleSide::DEFENDER);
	EXPECT_EQ(state.activeSide(), BattleSide::NONE);
}

TEST(BattleDeploymentOrderTest, LiveUpdatesCanOnlyCompleteTheCurrentOpportunity)
{
	const auto initial = twoSidedDeployment(BattleSide::DEFENDER);
	auto afterDefender = initial;
	afterDefender.complete(BattleSide::DEFENDER);
	EXPECT_NO_THROW(afterDefender.validateTransitionFrom(initial));

	auto changedOrder = initial;
	changedOrder.initialFirstSide = BattleSide::ATTACKER;
	EXPECT_THROW(changedOrder.validateTransitionFrom(initial), std::runtime_error);

	auto restarted = afterDefender;
	restarted.completed[BattleSide::DEFENDER] = false;
	EXPECT_THROW(restarted.validateTransitionFrom(afterDefender), std::runtime_error);

	auto skipped = initial;
	skipped.completed[BattleSide::ATTACKER] = true;
	EXPECT_THROW(skipped.validateTransitionFrom(initial), std::runtime_error);

	auto wrongSideCompletion = initial;
	EXPECT_THROW(wrongSideCompletion.complete(BattleSide::ATTACKER), std::runtime_error);
	EXPECT_EQ(wrongSideCompletion, initial);
}

TEST(BattleDeploymentOrderTest, InvalidAndDisabledOrdersAreRejected)
{
	auto invalidSide = twoSidedDeployment(BattleSide::DEFENDER);
	invalidSide.initialFirstSide = BattleSide::NONE;
	EXPECT_THROW(invalidSide.validateShape(), std::runtime_error);

	CMemorySerializer invalidWriter;
	invalidWriter.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalidWriter.oser & invalidSide, std::runtime_error);
	EXPECT_TRUE(invalidWriter.extractBuffer().empty());

	BattleDeploymentState disabled;
	disabled.initialFirstSide = BattleSide::DEFENDER;
	EXPECT_THROW(disabled.validateShape(), std::runtime_error);
	CMemorySerializer disabledWriter;
	disabledWriter.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(disabledWriter.oser & disabled, std::runtime_error);
	EXPECT_TRUE(disabledWriter.extractBuffer().empty());
}

TEST(BattleDeploymentOrderTest, CurrentOrderRoundTripsAndOlderReadersDefaultToAttacker)
{
	auto currentState = twoSidedDeployment(BattleSide::DEFENDER);
	currentState.completed[BattleSide::DEFENDER] = true;
	currentState.finalRelocationDistances[BattleSide::ATTACKER] = 1;
	currentState.finalRelocationDistances[BattleSide::DEFENDER] = 1;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & currentState);
	BattleDeploymentState currentDecoded;
	ASSERT_NO_THROW(current.iser & currentDecoded);
	EXPECT_EQ(currentDecoded, currentState);

	constexpr auto oldVersion = ESerializationVersion::NEW_HORIZONS_PORTAL_SOURCE;
	auto legacyDefault = twoSidedDeployment(BattleSide::ATTACKER);
	CMemorySerializer old;
	old.oser.version = oldVersion;
	old.iser.version = oldVersion;
	uint32_t before = 0x13579BDF;
	uint32_t after = 0x2468ACE0;
	ASSERT_NO_THROW(old.oser & before);
	ASSERT_NO_THROW(old.oser & legacyDefault);
	ASSERT_NO_THROW(old.oser & after);

	uint32_t decodedBefore = 0;
	uint32_t decodedAfter = 0;
	BattleDeploymentState legacyDecoded;
	legacyDecoded.initialFirstSide = BattleSide::DEFENDER; // Prove the old reader explicitly supplies its default.
	ASSERT_NO_THROW(old.iser & decodedBefore);
	ASSERT_NO_THROW(old.iser & legacyDecoded);
	ASSERT_NO_THROW(old.iser & decodedAfter);
	EXPECT_EQ(decodedBefore, before);
	EXPECT_EQ(decodedAfter, after);
	EXPECT_EQ(legacyDecoded.initialFirstSide, BattleSide::ATTACKER);
	EXPECT_EQ(legacyDecoded, legacyDefault);

	auto nonDefault = twoSidedDeployment(BattleSide::DEFENDER);
	CMemorySerializer oldWriter;
	oldWriter.oser.version = oldVersion;
	EXPECT_THROW(oldWriter.oser & nonDefault, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}
