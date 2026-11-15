/*
 * ReplayDayZeroMergeTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ReplayFixtureTextRegistration.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../client/replay/GameplayReplayer.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../server/CGameHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

class ReplayDayZeroMergeTest : public BattleTestFixture
{
protected:
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(replayFixture::registerMapStrings(gameState()->getMap()));
		gameState()->day = 0;
		gameState()->replayLog = ReplayLog();
		gameState()->replayLog.configure(true, 1);
	}

	SetHeroExperience experience(int amount)
	{
		SetHeroExperience pack;
		pack.id = attackerSideHero->id;
		pack.val = amount;
		return pack;
	}

	std::vector<std::byte> rngState()
	{
		CMemorySerializer wire;
		gameHandler->randomizer->serialize(wire.oser);
		dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(wire.oser);
		CRandomGenerator::getDefault().serialize(wire.oser);
		return wire.extractBuffer();
	}

	void applyPayload(CGameState & state, const ReplayPayload & payload)
	{
		auto decoded = ReplayPackSerializer::read(payload, &state);
		auto * packet = dynamic_cast<CPackForClient *>(decoded.get());
		ASSERT_NE(packet, nullptr);
		state.apply(*packet);
	}
};

TEST_F(ReplayDayZeroMergeTest, SetupPacketsAndFirstTurnKeepTheOriginalAnchorAndOrderedOrigins)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & state = *gameState();
	const auto rngBefore = rngState();
	const auto initial = state.saveReplaySnapshot();
	const auto initialExperience = attackerSideHero->exp;
	auto setup = experience(7);
	const auto setupPayload = ReplayPackSerializer::write(setup);
	state.apply(setup);
	ASSERT_EQ(state.replayLog.getChapters().size(), 1u);
	const auto * anchorStorage = state.replayLog.getChapters().front().gamestateSnapshot.data();
	NewTurn first;
	first.day = 1;
	const auto turnPayload = ReplayPackSerializer::write(first);
	state.apply(first);
	const auto & chapters = state.replayLog.getChapters();
	ASSERT_EQ(chapters.size(), 1u);
	EXPECT_EQ(chapters.front().day, 1u);
	EXPECT_EQ(chapters.front().snapshotPayload(), initial);
	EXPECT_EQ(chapters.front().gamestateSnapshot.data(), anchorStorage);
	ASSERT_EQ(chapters.front().packs.size(), 2u);
	EXPECT_EQ(chapters.front().packPayload(0), setupPayload);
	EXPECT_EQ(chapters.front().packPayload(1), turnPayload);
	EXPECT_EQ(state.day, 1);
	EXPECT_EQ(attackerSideHero->exp, initialExperience + 7);
	EXPECT_EQ(rngState(), rngBefore);
	EXPECT_TRUE(state.replayLog.canReplayEntireGame());
}

TEST_F(ReplayDayZeroMergeTest, FirstTurnSeekFastForwardsSetupAndRebuildsTheSameWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & state = *gameState();
	const auto rngBefore = rngState();
	auto setup = experience(7);
	state.apply(setup);
	NewTurn first;
	first.day = 1;
	state.apply(first);
	state.replayLog.addTurn(PlayerColor(0));
	auto duringTurn = experience(3);
	state.apply(duringTurn);
	state.replayLog.endTurn(PlayerColor(0));
	CMemorySerializer wire;
	wire.oser & state.replayLog;
	ASSERT_EQ(wire.oser.version, ESerializationVersion::CURRENT);
	CMemorySerializer input(wire.extractBuffer());
	input.iser.version = ESerializationVersion::CURRENT;
	ReplayLog loadedLog;
	input.iser & loadedLog;
	ASSERT_EQ(loadedLog.getChapters().size(), 1u);
	const auto & loaded = loadedLog.getChapters().front();
	EXPECT_EQ(loaded.day, 1u);
	EXPECT_EQ(loaded.snapshotPayload(), state.replayLog.getChapters().front().snapshotPayload());
	EXPECT_EQ(loaded.packs, state.replayLog.getChapters().front().packs);
	EXPECT_EQ(loaded.packVersions, state.replayLog.getChapters().front().packVersions);
	ASSERT_EQ(loaded.turns.size(), 1u);
	EXPECT_EQ(loaded.turns[0].player, PlayerColor(0));
	EXPECT_EQ(loaded.turns[0].firstPack, 2u);
	EXPECT_EQ(loaded.turns[0].lastPack, 3u);
	EXPECT_TRUE(loadedLog.canReplayEntireGame());
	const auto expected = state.saveReplaySnapshot();
	const auto options = ReplayPlanner::availableTurns(loadedLog);
	ASSERT_EQ(options.size(), 1u);
	EXPECT_EQ(options.front().day, 1u);
	const auto seek = ReplayPlanner::prepareTurn(loadedLog, options.front());
	ASSERT_EQ(seek.fastForwardPacks.size(), 2u);
	EXPECT_EQ(seek.fastForwardPacks[0], ReplayPackSerializer::write(setup));
	EXPECT_EQ(seek.fastForwardPacks[1], ReplayPackSerializer::write(first));
	ASSERT_EQ(seek.days.size(), 1u);
	ASSERT_EQ(seek.days.front().packs.size(), 1u);
	EXPECT_EQ(seek.days.front().packs.front(), ReplayPackSerializer::write(duringTurn));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadReplaySnapshot(seek.snapshot);
	for(const auto & payload : seek.fastForwardPacks)
		ASSERT_NO_FATAL_FAILURE(applyPayload(restored, payload));
	for(const auto & payload : seek.days.front().packs)
		ASSERT_NO_FATAL_FAILURE(applyPayload(restored, payload));
	EXPECT_EQ(restored.saveReplaySnapshot(), expected);
	const auto entire = ReplayPlanner::prepareEntireGame(loadedLog);
	ASSERT_EQ(entire.days.size(), 1u);
	ASSERT_EQ(entire.days.front().packs.size(), 3u);
	EXPECT_EQ(entire.days.front().packs[0], ReplayPackSerializer::write(setup));
	EXPECT_EQ(entire.days.front().packs[1], ReplayPackSerializer::write(first));
	EXPECT_EQ(entire.days.front().packs[2], ReplayPackSerializer::write(duringTurn));
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplayDayZeroMergeTest, MergedDayOneSimultaneousTurnsReplayFromTheInitialAnchor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & state = *gameState();
	const auto initial = state.saveReplaySnapshot();
	const auto rngBefore = rngState();
	auto setup = experience(7);
	state.apply(setup);
	NewTurn first;
	first.day = 1;
	state.apply(first);
	state.replayLog.addTurn(PlayerColor(0));
	auto red = experience(3);
	state.apply(red);
	state.replayLog.addTurn(PlayerColor(1));
	auto overlap = experience(5);
	state.apply(overlap);
	state.replayLog.endTurn(PlayerColor(0));
	auto blue = experience(2);
	state.apply(blue);
	state.replayLog.endTurn(PlayerColor(1));
	const auto options = ReplayPlanner::availableTurns(state.replayLog);
	ASSERT_EQ(options.size(), 2u);
	for(size_t index = 0; index < options.size(); ++index)
	{
		SCOPED_TRACE(index);
		EXPECT_EQ(options[index].day, 1u);
		const auto sequence = ReplayPlanner::prepareTurn(state.replayLog, options[index]);
		EXPECT_EQ(sequence.snapshot, initial);
		EXPECT_TRUE(sequence.fastForwardPacks.empty());
		ASSERT_EQ(sequence.days.size(), 1u);
		const auto & day = sequence.days.front();
		ASSERT_EQ(day.packs.size(), index == 0 ? 4u : 5u);
		EXPECT_EQ(day.packs[0], ReplayPackSerializer::write(setup));
		EXPECT_EQ(day.packs[1], ReplayPackSerializer::write(first));
		EXPECT_EQ(day.packs[2], ReplayPackSerializer::write(red));
		EXPECT_EQ(day.packs[3], ReplayPackSerializer::write(overlap));
		if(index == 1) EXPECT_EQ(day.packs[4], ReplayPackSerializer::write(blue));
		ASSERT_EQ(day.passes.size(), 1u);
		EXPECT_EQ(day.passes.front().followedPlayer, std::optional<PlayerColor>(PlayerColor(index)));
		EXPECT_EQ(day.passes.front().packCount, day.packs.size());
	}
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplayDayZeroMergeTest, LaterDaysKeepTheirOwnAnchorsAndOrdinaryRetention)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & state = *gameState();
	auto setup = experience(7);
	state.apply(setup);
	NewTurn first;
	first.day = 1;
	state.apply(first);
	const auto beforeSecondDay = state.saveReplaySnapshot();
	NewTurn second;
	second.day = 2;
	state.apply(second);
	const auto & beforeExpiry = state.replayLog.getChapters();
	ASSERT_EQ(beforeExpiry.size(), 2u);
	EXPECT_EQ(beforeExpiry[0].day, 1u);
	EXPECT_EQ(beforeExpiry[1].day, 2u);
	EXPECT_EQ(beforeExpiry[1].snapshotPayload(), beforeSecondDay);
	ASSERT_EQ(beforeExpiry[1].packs.size(), 1u);
	EXPECT_EQ(beforeExpiry[1].packPayload(0), ReplayPackSerializer::write(second));
	state.replayLog.configure(false, 0);
	const auto beforeThirdDay = state.saveReplaySnapshot();
	NewTurn third;
	third.day = 3;
	state.apply(third);
	const auto & chapters = state.replayLog.getChapters();
	ASSERT_EQ(chapters.size(), 1u);
	EXPECT_EQ(chapters.front().day, 3u);
	EXPECT_EQ(chapters.front().snapshotPayload(), beforeThirdDay);
	ASSERT_EQ(chapters.front().packs.size(), 1u);
	EXPECT_EQ(chapters.front().packPayload(0), ReplayPackSerializer::write(third));
	EXPECT_FALSE(state.replayLog.canReplayEntireGame());
}

TEST_F(ReplayDayZeroMergeTest, EmptyLogFirstNewTurnStillTakesItsPrePacketSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & state = *gameState();
	const auto before = state.saveReplaySnapshot();
	const auto rngBefore = rngState();
	NewTurn first;
	first.day = 1;
	state.apply(first);
	const auto & chapters = state.replayLog.getChapters();
	ASSERT_EQ(chapters.size(), 1u);
	EXPECT_EQ(chapters.front().day, 1u);
	EXPECT_EQ(chapters.front().snapshotPayload(), before);
	ASSERT_EQ(chapters.front().packs.size(), 1u);
	EXPECT_EQ(chapters.front().packPayload(0), ReplayPackSerializer::write(first));
	EXPECT_EQ(state.day, 1);
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplayDayZeroMergeTest, LoadedOpaqueDayZeroAnchorIsRetainedWithoutInventingProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ReplayLog legacy;
	legacy.configure(true, 1);
	legacy.beginDay(0, {std::byte{7}});
	legacy.addPack({std::byte{8}});
	CMemorySerializer wire;
	wire.oser & legacy;
	CMemorySerializer input(wire.extractBuffer());
	ReplayLog loaded;
	input.iser & loaded;
	loaded.reconfigureOnLoad(1);
	const auto original = loaded.getChapters().front().snapshotPayload();
	NewTurn first;
	first.day = 1;
	const auto worldBefore = gameState()->saveReplaySnapshot();
	loaded.recordPack(first, *gameState());
	const auto & chapters = loaded.getChapters();
	ASSERT_EQ(chapters.size(), 1u);
	EXPECT_EQ(chapters.front().day, 1u);
	EXPECT_EQ(chapters.front().snapshotPayload(), original);
	ASSERT_EQ(chapters.front().packVersions.size(), 2u);
	EXPECT_EQ(chapters.front().packVersions[0], ESerializationVersion::NONE);
	EXPECT_EQ(chapters.front().packPayload(1), ReplayPackSerializer::write(first));
	EXPECT_FALSE(loaded.canReplayEntireGame());
	EXPECT_EQ(gameState()->saveReplaySnapshot(), worldBefore);
}

TEST_F(ReplayDayZeroMergeTest, RetagInvalidatesPreparedAppendEvenWhenNewTurnEncodingThrows)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ReplayLog log;
	log.configure(true, 1);
	log.beginDay(0, {std::byte{7}});
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	PreparedReplayAppend pending(log, packet, 0, [] { return ReplayPayload{}; });
	ASSERT_TRUE(pending.readyToCommit());
	NewTurn invalid;
	invalid.day = 1;
	invalid.newHorizonsInvestorDailyGold[attackerSideHero->id] = 1; // Must be a multiple of 50.
	EXPECT_THROW(log.recordPack(invalid, *gameState()), std::runtime_error);
	EXPECT_EQ(log.getChapters().front().day, 1u); // Existing retag-before-encoding order.
	EXPECT_TRUE(log.getChapters().front().packs.empty());
	EXPECT_FALSE(pending.readyToCommit());
	EXPECT_FALSE(pending.commit());
	EXPECT_EQ(log.getChapters().front().gamestateSnapshot, (std::vector<std::byte>{std::byte{7}}));
}

TEST_F(ReplayDayZeroMergeTest, MalformedActiveProvenanceRefusesBeforeRetagOrAppend)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool mismatchedPackOrigins : {false, true})
	{
		SCOPED_TRACE(mismatchedPackOrigins);
		ReplayLog log;
		log.configure(true, 1);
		log.beginDay(0, {std::byte{7}});
		log.addPack({std::byte{8}});
		// Deliberately corrupt only this owned test graph, bypassing supported writers.
		auto & chapter = const_cast<ReplayChapter &>(log.getChapters().front());
		if(mismatchedPackOrigins)
			chapter.packVersions.clear();
		else
			chapter.snapshotVersion = static_cast<ESerializationVersion>(static_cast<int32_t>(ESerializationVersion::CURRENT) + 1);
		const auto snapshot = chapter.gamestateSnapshot;
		const auto packs = chapter.packs;
		const auto versions = chapter.packVersions;
		NewTurn first;
		first.day = 1;
		EXPECT_THROW(log.recordPack(first, *gameState()), std::runtime_error);
		EXPECT_EQ(chapter.day, 0u);
		EXPECT_EQ(chapter.gamestateSnapshot, snapshot);
		EXPECT_EQ(chapter.packs, packs);
		EXPECT_EQ(chapter.packVersions, versions);
	}
}
