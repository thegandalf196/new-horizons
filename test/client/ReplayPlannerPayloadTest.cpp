/*
 * ReplayPlannerPayloadTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/replay/GameplayReplayer.h"
#include "../../lib/gameState/ReplayLog.h"
#include <stdexcept>
namespace
{
using Version = ESerializationVersion;
ReplayPayload sample(int value, Version version)
{
	return {{static_cast<std::byte>(value)}, version}; // storage/selection only, never decoded
}
void turn(ReplayLog & log, Version first, Version second)
{
	log.addTurn(PlayerColor(0));
	log.addPack(sample(1, first));
	log.addPack(sample(2, second));
	log.endTurn(PlayerColor(0));
}
}
TEST(ReplayPlannerPayloadTest, CrossChapterFlatteningCarriesEveryIndependentVersion)
{
	ReplayLog log;
	log.configure(true, 0);
	log.beginDay(1, sample(8, Version::NEW_HORIZONS_FLY_DAY_END));
	turn(log, Version::NEW_HORIZONS_FLY_DAY_END, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
	log.beginDay(2, sample(9, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES));
	turn(log, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES, Version::CURRENT);
	log.beginDay(3, sample(10, Version::CURRENT)); // expires middle snapshot, retains its packet origins
	turn(log, Version::CURRENT, Version::CURRENT);
	const auto & middle = log.getChapters()[1];
	EXPECT_TRUE(middle.gamestateSnapshot.empty());
	EXPECT_EQ(middle.snapshotVersion, Version::NONE);
	EXPECT_EQ(middle.packVersions, (std::vector<Version>{Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES, Version::CURRENT}));
	const auto options = ReplayPlanner::availableTurns(log);
	ASSERT_EQ(options.size(), 3u);
	const auto sequence = ReplayPlanner::prepareTurn(log, options[1]);
	EXPECT_EQ(sequence.snapshot.version, Version::NEW_HORIZONS_FLY_DAY_END);
	ASSERT_EQ(sequence.fastForwardPacks.size(), 2u);
	EXPECT_EQ(sequence.fastForwardPacks[0].version, Version::NEW_HORIZONS_FLY_DAY_END);
	EXPECT_EQ(sequence.fastForwardPacks[1].version, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
	ASSERT_EQ(sequence.days.front().packs.size(), 2u);
	EXPECT_EQ(sequence.days.front().packs[0].version, Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
	EXPECT_EQ(sequence.days.front().packs[1].version, Version::CURRENT);
	const auto entire = ReplayPlanner::prepareEntireGame(log);
	ASSERT_EQ(entire.days.size(), 3u);
	EXPECT_EQ(entire.days[1].packs[1].version, Version::CURRENT);
}
TEST(ReplayPlannerPayloadTest, UnknownHistoryRefusesOnlySequencesThatNeedItAndNewKnownAnchorRecovers)
{
	ReplayLog log;
	log.configure(true, 2);
	log.beginDay(1, sample(8, Version::NONE));
	turn(log, Version::NONE, Version::CURRENT); // old opaque + new write in SAME chapter
	log.beginDay(2, sample(9, Version::CURRENT));
	turn(log, Version::CURRENT, Version::CURRENT);
	const auto options = ReplayPlanner::availableTurns(log);
	ASSERT_EQ(options.size(), 2u);
	EXPECT_THROW(ReplayPlanner::prepareTurn(log, options[0]), std::runtime_error);
	EXPECT_THROW(ReplayPlanner::prepareEntireGame(log), std::runtime_error);
	const auto later = ReplayPlanner::prepareTurn(log, options[1]);
	EXPECT_EQ(later.snapshot.version, Version::CURRENT);
	EXPECT_TRUE(later.fastForwardPacks.empty());
	EXPECT_EQ(log.getChapters()[0].gamestateSnapshot, sample(8, Version::NONE).bytes);
	EXPECT_EQ(log.getChapters()[0].packVersions, (std::vector<Version>{Version::NONE, Version::CURRENT}));
}
TEST(ReplayPlannerPayloadTest, SimultaneousPassesKeepOriginalPacketVersions)
{
	ReplayLog log;
	log.configure(true, 2);
	log.beginDay(1, sample(8, Version::NEW_HORIZONS_FLY_DAY_END));
	log.addTurn(PlayerColor(0));
	log.addPack(sample(1, Version::NEW_HORIZONS_FLY_DAY_END));
	log.addTurn(PlayerColor(1));
	log.addPack(sample(2, Version::CURRENT));
	log.endTurn(PlayerColor(0));
	log.addPack(sample(3, Version::CURRENT));
	log.endTurn(PlayerColor(1));
	const auto sequence = ReplayPlanner::prepareEntireGame(log);
	ASSERT_EQ(sequence.days.front().passes.size(), 2u);
	EXPECT_EQ(sequence.days.front().packs[0].version, Version::NEW_HORIZONS_FLY_DAY_END);
	EXPECT_EQ(sequence.days.front().packs[1].version, Version::CURRENT);
	EXPECT_EQ(sequence.days.front().passes[0].packCount, 2u);
	EXPECT_EQ(sequence.days.front().passes[1].packCount, 3u);
}

TEST(ReplayPlannerPayloadTest, UnknownUnneededMiddleSnapshotDoesNotHideKnownEarlierAnchor)
{
	ReplayLog log;
	log.configure(true, 2);
	log.beginDay(1, sample(8, Version::NEW_HORIZONS_FLY_DAY_END));
	turn(log, Version::NEW_HORIZONS_FLY_DAY_END, Version::CURRENT);
	log.beginDay(2, sample(9, Version::NONE));
	turn(log, Version::CURRENT, Version::CURRENT);
	const auto options = ReplayPlanner::availableTurns(log);
	const auto later = ReplayPlanner::prepareTurn(log, options.back());
	EXPECT_EQ(later.snapshot.version, Version::NEW_HORIZONS_FLY_DAY_END);
	EXPECT_EQ(later.fastForwardPacks.size(), 2u);
	EXPECT_TRUE(log.canReplayEntireGame());
	EXPECT_NO_THROW(ReplayPlanner::prepareEntireGame(log));
}
