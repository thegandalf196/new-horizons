/*
 * PreparedClientWorldReplayTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../client/replay/PreparedClientBattleReplay.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/BinarySerializer.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include <type_traits>

namespace
{
class PreparedClientWorldReplayTest : public HeroCommandFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		startBattle();
		first = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
		second = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(rightHex), 4);
	}
	void beginLog(bool configured)
	{
		auto & log = gameHandler->gs->replayLog;
		log = ReplayLog{};
		if(configured)
			log.configure(false, 2);
		log.beginDay(gameHandler->gs->day, {std::byte{0x11}}); // never replayed
	}
	BattleUnitsChanged defending()
	{
		BattleUnitsChanged packet;
		packet.battleID = battle()->getBattleID();
		packet.changedStacks.emplace_back(first->unitId(), BattleChanges::EOperation::UPDATE);
		packet.changedStacks.back().data = first->save();
		packet.changedStacks.back().data["state"]["defending"].Bool() = true;
		return packet;
	}
	std::vector<std::byte> bytes(const ReplayLog & log)
	{
		CMemorySerializer memory;
		memory.oser & log;
		return memory.extractBuffer();
	}
	void prepare(PreparedClientBattleReplay & entry)
	{
		TCNodesVector readOnly;
		// Only the changed native stack is writable in this exact fixture program.
		// Every other captured row remains explicitly read-only: no const promotion.
		for(const auto * node : entry.projectedWorld().capturedGraphNodes())
			if(node != first)
				readOnly.push_back(node);
		entry.prepareWorld({first}, readOnly);
	}
	NetworkLagPredictedPack predicted(const CPackForClient & packet)
	{
		NetworkLagPredictedPack result;
		BinarySerializer writer(&result);
		writer & &packet;
		return result;
	}
	void open(NetworkLagReplyState & predictions)
	{
		ArrangeStacks request;
		request.requestID = 5;
		request.player = PlayerColor(0);
		predictions.beginPrediction(request);
		PackageReceived received;
		received.requestID = request.requestID;
		received.player = request.player;
		predictions.openReplyPackage(received);
	}
};

TEST_F(PreparedClientWorldReplayTest, ActualWorldAdoptsOneOuterPlusConfiguredInnerBatchWithoutSecondProducer)
{
	for(bool configured : {false, true})
	{
		beginLog(configured);
		const auto initial = first->save();
		auto caller = defending();
		const auto input = caller.changedStacks.front().data;
		auto expectedCaller = caller;
		auto expectedLog = gameHandler->gs->replayLog;
		expectedLog.recordPack(expectedCaller, *gameHandler->gs);
		if(configured)
			expectedLog.recordPack(expectedCaller, *gameHandler->gs);
		const auto before = bytes(gameHandler->gs->replayLog);
		auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
		entry->adoptIncomingBattleWorld();
		EXPECT_FALSE(entry->commitReplayOnly());
		EXPECT_FALSE(entry->commitWorldAndReplay()); // not prepared: no replay prefix
		EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
		EXPECT_EQ(first->save(), initial);
		EXPECT_EQ(caller.changedStacks.front().data, input);
		EXPECT_THROW(NativeBonusGraphMutation{}, std::logic_error); // SAME live scope
		prepare(*entry);
		ASSERT_TRUE(entry->ready());
		static_assert(noexcept(entry->commitWorldAndReplay()));
		ASSERT_TRUE(entry->commitWorldAndReplay());
		EXPECT_TRUE(first->defending);
		EXPECT_EQ(first->save(), input); // canonical pre-writer state, not inserted null keys
		EXPECT_EQ(caller.changedStacks.front().data, expectedCaller.changedStacks.front().data);
		EXPECT_EQ(bytes(gameHandler->gs->replayLog), bytes(expectedLog));
		ASSERT_EQ(gameHandler->gs->replayLog.getChapters().back().packs.size(), configured ? 2 : 1);
		const auto finalGeneration = PreparedBonusGraphStorage::currentGeneration();
		EXPECT_FALSE(entry->commitWorldAndReplay());
		EXPECT_FALSE(entry->commitReplayOnly());
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), finalGeneration);
		entry.reset();
		first->load(initial); // complete this scalar restoration BEFORE next capture
	}
}

TEST_F(PreparedClientWorldReplayTest, MalformedLaterEntryAbandonsAdoptionWithoutWorldCallerOrReplayPrefix)
{
	beginLog(true);
	auto caller = defending();
	caller.changedStacks.emplace_back(second->unitId(), BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data = second->save();
	caller.changedStacks.back().data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	const auto input = caller.changedStacks;
	const auto a = first->save(), b = second->save();
	const auto before = bytes(gameHandler->gs->replayLog);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
	EXPECT_THROW(entry->adoptIncomingBattleWorld(), std::runtime_error);
	EXPECT_FALSE(entry->ready());
	EXPECT_FALSE(entry->commitReplayOnly());
	EXPECT_FALSE(entry->commitWorldAndReplay());
	EXPECT_EQ(first->save(), a);
	EXPECT_EQ(second->save(), b);
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
	ASSERT_EQ(caller.changedStacks.size(), input.size());
	for(size_t i = 0; i < input.size(); ++i)
		EXPECT_EQ(caller.changedStacks[i].data, input[i].data);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_NO_THROW(NativeBonusGraphMutation{}); // failed owner released its scope
}

TEST_F(PreparedClientWorldReplayTest, StaleLogCannotTransferIntoWorldOrAcceptStandalonePrefix)
{
	beginLog(true);
	auto caller = defending();
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
	gameHandler->gs->replayLog.addPack({std::byte{8}});
	const auto changed = bytes(gameHandler->gs->replayLog);
	EXPECT_THROW(entry->adoptIncomingBattleWorld(), std::logic_error);
	EXPECT_FALSE(entry->commitReplayOnly());
	EXPECT_FALSE(entry->commitWorldAndReplay());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), changed);
}

TEST_F(PreparedClientWorldReplayTest, CallerFreshnessStillRefusesAfterBatchAndScopeMoveToWorld)
{
	beginLog(true);
	auto caller = defending();
	const auto before = bytes(gameHandler->gs->replayLog);
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
	entry->adoptIncomingBattleWorld();
	prepare(*entry);
	caller.battleID = BattleID(99999);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(entry->ready());
	EXPECT_FALSE(entry->commitWorldAndReplay());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(caller.battleID, BattleID(99999));
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedClientWorldReplayTest, NativeWorldRefusalCannotBeBypassedByReplayOnlyEntry)
{
	beginLog(true);
	auto caller = defending();
	const auto before = bytes(gameHandler->gs->replayLog);
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
	entry->adoptIncomingBattleWorld();
	prepare(*entry);
	battle()->bloodrageFirstBloodUsed = !battle()->bloodrageFirstBloodUsed;
	const auto changed = battle()->bloodrageFirstBloodUsed;
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(entry->commitWorldAndReplay());
	EXPECT_FALSE(entry->commitReplayOnly());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(battle()->bloodrageFirstBloodUsed, changed);
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedClientWorldReplayTest, ActualInspectionFreshnessStillRefusesAfterWorldAdoption)
{
	beginLog(true);
	auto caller = defending();
	NetworkLagReplyState predictions; // no active package: ordinary APPLY decision
	const auto before = bytes(gameHandler->gs->replayLog);
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, &predictions);
	entry->adoptIncomingBattleWorld();
	prepare(*entry);
	predictions.invalidateInspection();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(entry->ready());
	EXPECT_FALSE(entry->commitWorldAndReplay());
	EXPECT_FALSE(entry->commitReplayOnly());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedClientWorldReplayTest, RetirementRetainsThenReleasesClientDataAndRootWorldOwners)
{
	beginLog(false);
	auto caller = defending();
	const auto priorOwners = gameHandler->gs.use_count();
	const auto before = bytes(gameHandler->gs->replayLog);
	auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, nullptr);
	entry->adoptIncomingBattleWorld();
	// Client origin + Data-until-state-unlock + Pin-until-root-unlock ownership.
	EXPECT_EQ(gameHandler->gs.use_count(), priorOwners + 3);
	EXPECT_THROW(NativeBonusGraphMutation{}, std::logic_error);
	entry.reset(); // all arena/participant keys retire before native/state/world
	EXPECT_EQ(gameHandler->gs.use_count(), priorOwners);
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
}

TEST_F(PreparedClientWorldReplayTest, PendingConsumeAndAdventureRollbackAreNotReplacedByIncomingOnlyProgram)
{
	for(bool match : {true, false})
	{
		beginLog(true);
		auto caller = defending();
		NetworkLagReplyState predictions;
		open(predictions);
		auto writer = caller;
		(void)ReplayPackSerializer::write(writer); // actual outer precedes matcher
		auto prediction = predicted(writer);
		if(!match)
		{
			prediction.predictedPackData.clear();
			auto rollback = std::make_unique<HeroVisitCastle>();
			rollback->hid = ObjectInstanceID(1); rollback->tid = ObjectInstanceID(2); rollback->startVisit = true;
			prediction.rollbackPacks.push_back(std::move(rollback));
		}
		predictions.appendPrediction(std::move(prediction));
		const auto before = bytes(gameHandler->gs->replayLog);
		auto entry = prepareClientBattleReplayEntry(gameHandler->gs, caller, &predictions);
		EXPECT_EQ(entry->replyAction(), match ? BattleReplyInspection::Result::CONSUMED : BattleReplyInspection::Result::ROLLBACK_THEN_APPLY);
		EXPECT_THROW(entry->adoptIncomingBattleWorld(), std::logic_error);
		EXPECT_TRUE(entry->ready()); // original inspection/batch retained, not consumed
		EXPECT_EQ(entry->rollbackCount(), match ? 0 : 1);
		EXPECT_FALSE(entry->commitWorldAndReplay());
		EXPECT_FALSE(first->defending);
		EXPECT_EQ(bytes(gameHandler->gs->replayLog), before);
	}
}
}
