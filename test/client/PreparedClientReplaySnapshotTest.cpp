/*
 * PreparedClientReplaySnapshotTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../client/replay/PreparedClientBattleReplay.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"

class PreparedClientReplaySnapshotTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

TEST_F(PreparedClientReplaySnapshotTest, EmptyUnconfiguredLogCapturesActualPrewriterState)
{
	startGame();
	auto state = gameState();
	state->replayLog = ReplayLog{};
	const auto expectedSnapshot = state->saveToMemory();
	ASSERT_TRUE(state->replayLog.empty());
	ASSERT_FALSE(state->replayLog.isRecordingPacks());
	BattleUnitsChanged caller;
	caller.battleID = BattleID(0);
	caller.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["defending"].Bool() = true;
	const auto input = caller.changedStacks.back().data;
	auto scalar = caller;
	const auto expectedPack = ReplayPackSerializer::write(scalar);
	auto entry = prepareClientBattleReplayEntry(state, caller, nullptr);
	EXPECT_TRUE(state->replayLog.empty());
	EXPECT_EQ(caller.changedStacks.back().data, input);
	ASSERT_TRUE(entry->commitReplayOnly());
	ASSERT_EQ(state->replayLog.getChapters().size(), 1);
	const auto & chapter = state->replayLog.getChapters().front();
	EXPECT_EQ(chapter.gamestateSnapshot, expectedSnapshot);
	ASSERT_EQ(chapter.packs.size(), 1);
	EXPECT_EQ(chapter.packs.front(), expectedPack);
	EXPECT_FALSE(state->replayLog.isRecordingPacks());
	EXPECT_EQ(caller.changedStacks.back().data, scalar.changedStacks.back().data);
}

TEST_F(PreparedClientReplaySnapshotTest, WriterFailureAfterActualEmptySnapshotLeavesNoChapterOrCallerPrefix)
{
	startGame();
	auto state = gameState();
	state->replayLog = ReplayLog{};
	const auto before = state->saveToMemory();
	BattleUnitsChanged caller;
	caller.battleID = BattleID(0);
	caller.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["luckyOwnAttackSequence"].String() = "invalid";
	const auto input = caller.changedStacks.back().data;
	EXPECT_THROW(prepareClientBattleReplayEntry(state, caller, nullptr), std::runtime_error);
	EXPECT_TRUE(state->replayLog.empty());
	EXPECT_FALSE(state->replayLog.isRecordingPacks());
	EXPECT_EQ(caller.changedStacks.back().data, input);
	EXPECT_EQ(state->saveToMemory(), before);
}
