/*
 * PreparedClientBattleReplayTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/replay/PreparedClientBattleReplay.h"
#include "../../client/netlag/NetworkLagRollbackCopy.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/BinarySerializer.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"

namespace
{
std::vector<std::byte> replayBytes(const ReplayLog & log)
{
	CMemorySerializer memory;
	memory.oser & log;
	return memory.extractBuffer();
}

std::shared_ptr<CGameState> world(bool configured)
{
	auto state = std::make_shared<CGameState>();
	state->day = 3;
	if(configured) state->replayLog.configure(false, 2);
	state->replayLog.beginDay(3, {std::byte{7}}); // existing chapter avoids invalid empty-world save
	return state;
}

BattleUnitsChanged packet()
{
	BattleUnitsChanged p;
	p.battleID = BattleID(0);
	return p;
}

NetworkLagPredictedPack predicted(const CPackForClient & packet)
{
	NetworkLagPredictedPack result;
	BinarySerializer serializer(&result);
	serializer & &packet;
	return result;
}

void openPrediction(NetworkLagReplyState & state)
{
	ArrangeStacks request;
	request.requestID = 5;
	request.player = PlayerColor(0);
	state.beginPrediction(request);
	PackageReceived received;
	received.requestID = 5;
	received.player = PlayerColor(0);
	state.openReplyPackage(received);
}
}

TEST(PreparedClientBattleReplayTest, ActualNoPredictionEntryPreservesUnconditionalOuterAndConfiguredInner)
{
	for(bool configured : {false, true})
	{
		auto state = world(configured);
		auto expected = state->replayLog;
		auto caller = packet();
		auto scalar = caller;
		expected.recordPack(scalar, *state); // actual outer API, independent of mode
		if(configured) expected.recordPack(scalar, *state); // actual GS.apply inner policy
		const auto before = replayBytes(state->replayLog);
		auto entry = prepareClientBattleReplayEntry(state, caller, nullptr);
		EXPECT_EQ(replayBytes(state->replayLog), before);
		EXPECT_EQ(entry->replyAction(), BattleReplyInspection::Result::APPLY);
		ASSERT_TRUE(entry->commitReplayOnly());
		EXPECT_EQ(replayBytes(state->replayLog), replayBytes(expected));
		EXPECT_FALSE(entry->commitReplayOnly());
	}
}

TEST(PreparedClientBattleReplayTest, RealPredictionMatchRecordsOnlyOuterRegardlessOfConfiguredMode)
{
	for(bool configured : {false, true})
	{
		auto state = world(configured);
		auto caller = packet();
		NetworkLagReplyState predictions;
		openPrediction(predictions);
		predictions.appendPrediction(predicted(caller));
		auto expected = state->replayLog;
		auto scalar = caller;
		expected.recordPack(scalar, *state);
		auto entry = prepareClientBattleReplayEntry(state, caller, &predictions);
		EXPECT_EQ(entry->replyAction(), BattleReplyInspection::Result::CONSUMED);
		EXPECT_EQ(entry->rollbackCount(), 0);
		ASSERT_TRUE(entry->commitReplayOnly());
		EXPECT_EQ(replayBytes(state->replayLog), replayBytes(expected));
	}
}

TEST(PreparedClientBattleReplayTest, RealMismatchUsesReversePredictionsAndForwardRollbackOrder)
{
	for(bool configured : {false, true})
	{
		auto state = world(configured);
		auto caller = packet();
		NetworkLagReplyState predictions;
		openPrediction(predictions);
		HeroVisitCastle old;
		old.startVisit = true;
		old.hid = ObjectInstanceID(1);
		old.tid = ObjectInstanceID(2);
		auto first = predicted(old), second = predicted(old);
		for(int id : {3, 4})
		{
			auto rollback = std::make_unique<HeroVisitCastle>(old);
			rollback->hid = ObjectInstanceID(id);
			first.rollbackPacks.push_back(std::move(rollback));
		}
		second.rollbackPacks.push_back(std::make_unique<HeroVisitCastle>(old));
		predictions.appendPrediction(std::move(first));
		predictions.appendPrediction(std::move(second));
		auto expected = state->replayLog;
		auto scalar = caller;
		expected.recordPack(scalar, *state);
		if(configured)
		{
			for(int id : {1, 3, 4}) { auto r = old; r.hid = ObjectInstanceID(id); expected.recordPack(r, *state); }
			expected.recordPack(scalar, *state);
		}
		auto entry = prepareClientBattleReplayEntry(state, caller, &predictions);
		EXPECT_EQ(entry->replyAction(), BattleReplyInspection::Result::ROLLBACK_THEN_APPLY);
		EXPECT_EQ(entry->rollbackCount(), 3);
		ASSERT_TRUE(entry->commitReplayOnly());
		EXPECT_EQ(replayBytes(state->replayLog), replayBytes(expected));
	}
}

TEST(PreparedClientBattleReplayTest, RealQueueInvalidationOrCallerMutationRefusesWholeReplay)
{
	auto state = world(true);
	auto caller = packet();
	NetworkLagReplyState predictions;
	const auto before = replayBytes(state->replayLog);
	{
		auto entry = prepareClientBattleReplayEntry(state, caller, &predictions);
		predictions.invalidateInspection();
		EXPECT_FALSE(entry->commitReplayOnly());
		EXPECT_EQ(replayBytes(state->replayLog), before);
	}
	{
		auto entry = prepareClientBattleReplayEntry(state, caller, nullptr);
		caller.battleID = BattleID(99);
		EXPECT_FALSE(entry->commitReplayOnly());
		EXPECT_EQ(caller.battleID, BattleID(99));
		EXPECT_EQ(replayBytes(state->replayLog), before);
	}
}

TEST(PreparedClientBattleReplayTest, InspectorRetirementDoesNotLeaveAnOriginalQueuePointer)
{
	BattleReplyInspection inspection;
	{
		NetworkLagReplyState predictions;
		inspection = predictions.inspectBattleReply(packet());
		EXPECT_TRUE(inspection.stillCurrent());
	}
	EXPECT_FALSE(inspection.stillCurrent());
}

TEST(PreparedClientBattleReplayTest, CallerWriterProjectionStaysPrivateUntilWholeReplayAcceptance)
{
	auto state = world(true);
	auto caller = packet();
	caller.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["defending"].Bool() = true;
	const auto input = caller.changedStacks.back().data;
	auto expectedCaller = caller;
	auto expected = state->replayLog;
	expected.recordPack(expectedCaller, *state);
	expected.recordPack(expectedCaller, *state);
	ASSERT_NE(expectedCaller.changedStacks.back().data, input);
	const auto before = replayBytes(state->replayLog);
	auto entry = prepareClientBattleReplayEntry(state, caller, nullptr);
	EXPECT_EQ(caller.changedStacks.back().data, input);
	EXPECT_EQ(replayBytes(state->replayLog), before);
	ASSERT_TRUE(entry->commitReplayOnly());
	EXPECT_EQ(caller.changedStacks.back().data, expectedCaller.changedStacks.back().data);
	EXPECT_EQ(replayBytes(state->replayLog), replayBytes(expected));
}

TEST(PreparedClientBattleReplayTest, EveryCurrentRollbackProducerHasAnIndependentExactTypedCopy)
{
	SwapStacks swap;
	swap.srcArmy = ObjectInstanceID(1); swap.dstArmy = ObjectInstanceID(2);
	swap.srcSlot = SlotID(0); swap.dstSlot = SlotID(1);
	RebalanceStacks rebalance;
	rebalance.srcArmy = ObjectInstanceID(1); rebalance.dstArmy = ObjectInstanceID(2);
	rebalance.srcSlot = SlotID(0); rebalance.dstSlot = SlotID(1); rebalance.count = 3;
	HeroVisitCastle visit;
	visit.hid = ObjectInstanceID(1); visit.tid = ObjectInstanceID(2); visit.startVisit = true;
	TryMoveHero move;
	move.id = ObjectInstanceID(1); move.start = int3(1, 1, 0); move.end = int3(2, 1, 0);
	move.attackedFrom = int3(-1, -1, -1);
	FoWChange fow;
	fow.player = PlayerColor(0); fow.mode = ETileVisibility::HIDDEN;
	const CPackForClient * originals[]{&swap, &rebalance, &visit, &move, &fow};
	for(const auto * original : originals)
	{
		auto copy = copyCurrentPredictionRollback(*original);
		ASSERT_NE(copy.get(), original);
		EXPECT_EQ(typeid(*copy), typeid(*original));
		EXPECT_EQ(predicted(*copy).predictedPackData, predicted(*original).predictedPackData);
	}
	auto copy = copyCurrentPredictionRollback(visit);
	visit.hid = ObjectInstanceID(99);
	EXPECT_EQ(static_cast<HeroVisitCastle &>(*copy).hid, ObjectInstanceID(1));
	EXPECT_THROW(copyCurrentPredictionRollback(packet()), std::logic_error);
}
