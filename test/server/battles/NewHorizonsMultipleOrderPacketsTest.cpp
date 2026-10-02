/*
 * NewHorizonsMultipleOrderPacketsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
HeroOrderState makeOrder(HeroCommand command, int32_t issuedRound = 3)
{
	HeroOrderState result;
	result.command = command;
	result.issuedRound = issuedRound;
	return result;
}

StartAction makeOrderAction(bool preserveOtherOrders)
{
	StartAction result;
	result.battleID = BattleID(7);
	result.ba.actionType = EActionType::HERO_COMMAND;
	result.ba.side = BattleSide::ATTACKER;
	result.ba.command = HeroCommand::RIPOSTE;
	result.ba.stackNumber = std::numeric_limits<uint32_t>::max();
	result.orderState = makeOrder(HeroCommand::RIPOSTE);
	result.preserveOtherOrders = preserveOtherOrders;
	return result;
}

BattleHeroOrderStateChanged makeStateUpdate(std::vector<HeroOrderState> states)
{
	BattleHeroOrderStateChanged result;
	result.battleID = BattleID(7);
	result.side = BattleSide::ATTACKER;
	result.states = std::move(states);
	if(!result.states->empty())
		result.state = result.states->back();
	return result;
}

void updateLatestProjection(BattleHeroOrderStateChanged & packet)
{
	if(packet.states && !packet.states->empty())
		packet.state = packet.states->back();
	else
		packet.state.reset();
}

void useCurrentVersion(CMemorySerializer & serializer)
{
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
}

void useLastSingleOrderVersion(CMemorySerializer & serializer)
{
	serializer.oser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	serializer.iser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
}
}

class NewHorizonsMultipleOrderVisitorTest : public HeroCommandFixture {};

TEST(NewHorizonsMultipleOrderPacketsTest, StartActionPreservationRoundTripsAndCannotBeWrittenToOlderFormat)
{
	const auto outgoing = makeOrderAction(true);

	CMemorySerializer current;
	useCurrentVersion(current);
	ASSERT_NO_THROW(current.oser & outgoing);
	StartAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.preserveOtherOrders);
	ASSERT_TRUE(restored.orderState);
	EXPECT_EQ(restored.orderState->command, HeroCommand::RIPOSTE);

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	EXPECT_THROW(older.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
}

TEST(NewHorizonsMultipleOrderPacketsTest, CurrentStateUpdateRoundTripsFullCollectionAndEmptyClear)
{
	const auto charge = makeOrder(HeroCommand::CHARGE);
	const auto riposte = makeOrder(HeroCommand::RIPOSTE);
	const auto outgoing = makeStateUpdate({charge, riposte});

	CMemorySerializer current;
	useCurrentVersion(current);
	ASSERT_NO_THROW(current.oser & outgoing);
	BattleHeroOrderStateChanged restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_TRUE(restored.states);
	ASSERT_EQ(restored.states->size(), 2u);
	EXPECT_EQ(*restored.states, *outgoing.states);
	ASSERT_TRUE(restored.state);
	EXPECT_EQ(*restored.state, riposte);

	const auto clear = makeStateUpdate({});
	CMemorySerializer clearWire;
	useCurrentVersion(clearWire);
	ASSERT_NO_THROW(clearWire.oser & clear);
	BattleHeroOrderStateChanged restoredClear;
	ASSERT_NO_THROW(clearWire.iser & restoredClear);
	ASSERT_TRUE(restoredClear.states);
	EXPECT_TRUE(restoredClear.states->empty());
	EXPECT_FALSE(restoredClear.state);
}

TEST(NewHorizonsMultipleOrderPacketsTest, DuplicateAndMismatchedCollectionsAreRejectedBeforeWriting)
{
	const auto charge = makeOrder(HeroCommand::CHARGE);
	const auto riposte = makeOrder(HeroCommand::RIPOSTE);

	auto duplicate = makeStateUpdate({charge, charge});
	CMemorySerializer duplicateWire;
	useCurrentVersion(duplicateWire);
	EXPECT_THROW(duplicateWire.oser & duplicate, std::runtime_error);
	EXPECT_TRUE(duplicateWire.extractBuffer().empty());

	auto mismatchedProjection = makeStateUpdate({charge, riposte});
	mismatchedProjection.state = charge;
	CMemorySerializer projectionWire;
	useCurrentVersion(projectionWire);
	EXPECT_THROW(projectionWire.oser & mismatchedProjection, std::runtime_error);
	EXPECT_TRUE(projectionWire.extractBuffer().empty());
}

TEST(NewHorizonsMultipleOrderPacketsTest, MultiOrderDownsaveRejectsBeforeBytesAndSingletonMigrates)
{
	const auto charge = makeOrder(HeroCommand::CHARGE);
	const auto riposte = makeOrder(HeroCommand::RIPOSTE);
	const auto multiple = makeStateUpdate({charge, riposte});

	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	EXPECT_THROW(rejected.oser & multiple, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());

	const auto singleton = makeStateUpdate({charge});
	CMemorySerializer older;
	useLastSingleOrderVersion(older);
	ASSERT_NO_THROW(older.oser & singleton);
	BattleHeroOrderStateChanged restored;
	ASSERT_NO_THROW(older.iser & restored);
	ASSERT_TRUE(restored.states);
	ASSERT_EQ(restored.states->size(), 1u);
	EXPECT_EQ(restored.states->front(), charge);
	ASSERT_TRUE(restored.state);
	EXPECT_EQ(*restored.state, charge);
}

TEST_F(NewHorizonsMultipleOrderVisitorTest, ClientVisitorRejectsMissingReferencesButAllowsOnlyForwardProgress)
{
	prepareCommands();
	const auto round = battle()->getRound();
	const auto ownStacks = battle()->getStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER;
	});
	const auto enemyStacks = battle()->getStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER;
	});
	ASSERT_FALSE(ownStacks.empty());
	ASSERT_FALSE(enemyStacks.empty());
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1);
	ASSERT_NE(ward, nullptr);

	const auto charge = makeOrder(HeroCommand::CHARGE, round);
	auto protect = makeOrder(HeroCommand::PROTECT, round);
	protect.primaryTargetUnitId = ownStacks.front()->unitId();
	protect.secondaryTargetUnitId = ward->unitId();
	auto flank = makeOrder(HeroCommand::FLANK, round);
	flank.primaryTargetUnitId = enemyStacks.front()->unitId();
	flank.flankTargets.push_back({flank.primaryTargetUnitId, 0});
	const std::vector<HeroOrderState> activeOrders{charge, protect, flank};
	battle()->setHeroOrderStates(BattleSide::ATTACKER, activeOrders);

	const auto missingUnitId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	ASSERT_EQ(battle()->getStack(static_cast<int>(missingUnitId), false), nullptr);
	auto missingReference = makeStateUpdate(activeOrders);
	missingReference.battleID = battle()->getBattleID();
	missingReference.states->at(0).consumedUnitIds.push_back(missingUnitId);
	missingReference.states->at(1).protectInterceptionsConsumed = 1;
	missingReference.states->at(1).protectBroken = true;
	missingReference.states->at(2).flankTargets.front().sideMask = 1;
	updateLatestProjection(missingReference);
	BattleStatePackVisitor battleVisitor(*battle());
	EXPECT_THROW(battleVisitor.visitBattleHeroOrderStateChanged(missingReference), std::runtime_error);
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), activeOrders);

	auto validProgress = makeStateUpdate(activeOrders);
	validProgress.battleID = battle()->getBattleID();
	validProgress.states->at(0).consumedUnitIds.push_back(ownStacks.front()->unitId());
	validProgress.states->at(1).protectInterceptionsConsumed = 1;
	validProgress.states->at(1).protectBroken = true;
	validProgress.states->at(2).flankTargets.front().sideMask = 1;
	updateLatestProjection(validProgress);
	ASSERT_NO_THROW(battleVisitor.visitBattleHeroOrderStateChanged(validProgress));
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), *validProgress.states);

	auto regressedProgress = makeStateUpdate(activeOrders);
	regressedProgress.battleID = battle()->getBattleID();
	EXPECT_THROW(battleVisitor.visitBattleHeroOrderStateChanged(regressedProgress), std::runtime_error);
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), *validProgress.states);
}
