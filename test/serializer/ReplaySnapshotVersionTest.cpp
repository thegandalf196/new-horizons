/*
 * ReplaySnapshotVersionTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ReplayFixtureTextRegistration.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../server/CGameHandler.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>

class ReplaySnapshotVersionTest : public BattleTestFixture
{
protected:
	void startGame()
	{
		ASSERT_NO_FATAL_FAILURE(BattleTestFixture::startGame());
		ASSERT_NO_FATAL_FAILURE(replayFixture::registerMapStrings(gameState()->getMap()));
		// The battle fixture removes Knowledge after initializing Normal Spell Points.
		// Snapshot a valid current-capacity state, as ordinary packet/load boundaries do.
		const auto rngBefore = rngState();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			ASSERT_NE(hero, nullptr);
			const auto normalBefore = hero->getNormalSpellPoints();
			const auto bufferBefore = hero->getBufferSpellPoints();
			const auto maximum = hero->manaLimit();
			hero->clampSpellPointsToCapacity();
			EXPECT_EQ(hero->getNormalSpellPoints(), std::min(normalBefore, maximum));
			EXPECT_LE(hero->getNormalSpellPoints(), maximum);
			EXPECT_EQ(hero->getBufferSpellPoints(), bufferBefore);
		}
		EXPECT_EQ(rngState(), rngBefore);
	}

	std::vector<std::byte> rngState()
	{
		CMemorySerializer wire;
		gameHandler->randomizer->serialize(wire.oser);
		dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(wire.oser);
		CRandomGenerator::getDefault().serialize(wire.oser);
		return wire.extractBuffer();
	}
};

TEST_F(ReplaySnapshotVersionTest, UnknownSnapshotRefusesBeforeChangingExistingWorld)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto rngBefore = rngState();
	const auto before = gameState()->saveReplaySnapshot();
	const auto * originalHero = attackerSideHero;
	const auto originalId = originalHero->id;
	EXPECT_THROW(gameState()->loadReplaySnapshot(ReplayPayload{{std::byte{1}}}), std::runtime_error);
	EXPECT_EQ(gameState()->saveReplaySnapshot(), before);
	EXPECT_EQ(gameState()->getHero(originalId), originalHero);
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplaySnapshotVersionTest, PriorSnapshotOriginsRejectBeforeWorldOrRngMutation)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto before = gameState()->saveReplaySnapshot();
	const auto rngBefore = rngState();
	const auto * hero = attackerSideHero;
	const auto id = hero->id;
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_FLY_DAY_END,
		ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES})
	{
		SCOPED_TRACE(static_cast<int32_t>(version));
		EXPECT_THROW(gameState()->loadReplaySnapshot({{std::byte{1}}, version}), std::runtime_error);
		EXPECT_EQ(gameState()->saveReplaySnapshot(), before);
		EXPECT_EQ(gameState()->getHero(id), hero);
		EXPECT_EQ(rngState(), rngBefore);
	}
}

TEST_F(ReplaySnapshotVersionTest, CurrentSnapshotRecaptureKeepsLocalRewindVersionAndRng)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->nextBattleID = BattleID(9);
	const auto rngBefore = rngState();
	const auto original = gameState()->saveReplaySnapshot();
	ASSERT_EQ(original.version, ESerializationVersion::CURRENT);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadReplaySnapshot(original);
	EXPECT_EQ(restored.nextBattleID, BattleID(9));
	const auto local = restored.saveReplaySnapshot();
	if(local.bytes != original.bytes)
	{
		const auto differing = std::mismatch(local.bytes.begin(), local.bytes.end(), original.bytes.begin(), original.bytes.end());
		RecordProperty("originalSnapshotBytes", std::to_string(original.bytes.size()));
		RecordProperty("recapturedSnapshotBytes", std::to_string(local.bytes.size()));
		RecordProperty("firstSnapshotDifference", std::to_string(differing.first - local.bytes.begin()));
	}
	EXPECT_EQ(local, original);
	restored.loadReplaySnapshot(local);
	EXPECT_EQ(restored.saveReplaySnapshot(), local);
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplaySnapshotVersionTest, OpaqueReplaySurvivesCurrentWorldSnapshotLoadAndCurrentResave)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->replayLog = ReplayLog();
	gameState()->replayLog.beginDay(gameState()->day, {std::byte{7}});
	gameState()->replayLog.addPack({std::byte{8}});
	CMemorySerializer envelope;
	envelope.oser & *gameState();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadReplaySnapshot(ReplayPayload{envelope.extractBuffer(), envelope.oser.version});
	ASSERT_EQ(restored.replayLog.getChapters().size(), 1u);
	const auto & opaque = restored.replayLog.getChapters().front();
	EXPECT_EQ(opaque.gamestateSnapshot, (std::vector<std::byte>{std::byte{7}}));
	EXPECT_EQ(opaque.packs, (std::vector<std::vector<std::byte>>{{std::byte{8}}}));
	EXPECT_EQ(opaque.snapshotVersion, ESerializationVersion::NONE);
	EXPECT_EQ(opaque.packVersions, (std::vector<ESerializationVersion>{ESerializationVersion::NONE}));
	EXPECT_EQ(restored.day, gameState()->day);
	// Ordinary current saves retain opaque history; only replay execution refuses it.
	CMemorySerializer current;
	current.oser & restored;
	EXPECT_FALSE(current.extractBuffer().empty());
	restored.replayLog.reconfigureOnLoad(2);
	restored.replayLog.beginDay(restored.day + 1, restored.saveReplaySnapshot());
	EXPECT_EQ(restored.replayLog.getChapters().back().snapshotVersion, ESerializationVersion::CURRENT);
}

TEST_F(ReplaySnapshotVersionTest, InstalledCurrentPoliciesRefuseHistoricalWriterBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
	EXPECT_THROW(old.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
