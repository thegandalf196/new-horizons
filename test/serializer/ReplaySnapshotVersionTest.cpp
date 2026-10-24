/*
 * ReplaySnapshotVersionTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

class ReplaySnapshotVersionTest : public HeroCommandFixture
{
protected:
	bool historicalCapture = false;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(!historicalCapture)
			return;
		auto rules = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS);
		rules.setOverrideFlag(true);
		for(const auto key : {"recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
			rules.Struct().erase(key);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void expectHistoricalCapture(const CGameState & world)
	{
		for(const auto key : {"recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
			EXPECT_TRUE(world.getHeroDevelopmentRules()[key].isNull());
		EXPECT_EQ(world.getHeroDevelopmentRules(), gameState()->getHeroDevelopmentRules());
		const auto * hero = world.getHero(attackerSideHero->id);
		ASSERT_NE(hero, nullptr);
		for(const auto key : {"recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty"})
			EXPECT_TRUE(hero->getPrimaryGrowthRules()[key].isNull());
		EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
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

TEST_F(ReplaySnapshotVersionTest, HistoricalNativeSnapshotRecaptureUsesCurrentLocalRewindVersion)
{
	historicalCapture = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(expectHistoricalCapture(*gameState()));
	gameState()->replayLog = ReplayLog();
	gameState()->nextBattleID = BattleID(9);
	const auto rngBefore = rngState();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
	old.oser & *gameState();
	ReplayPayload historical{old.extractBuffer(), old.oser.version};
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadReplaySnapshot(historical);
	ASSERT_NO_FATAL_FAILURE(expectHistoricalCapture(restored));
	EXPECT_EQ(restored.day, gameState()->day);
	EXPECT_EQ(restored.nextBattleID, BattleID(9));
	EXPECT_EQ(restored.getMagicRules(), gameState()->getMagicRules());
	const auto localDayStart = restored.saveReplaySnapshot();
	EXPECT_EQ(localDayStart.version, ESerializationVersion::CURRENT);
	EXPECT_NE(localDayStart.version, historical.version);
	restored.loadReplaySnapshot(localDayStart); // a later simturn pass reads its local writer, not the historical origin
	EXPECT_EQ(restored.saveReplaySnapshot(), localDayStart);
	EXPECT_EQ(rngState(), rngBefore);
}

TEST_F(ReplaySnapshotVersionTest, LegacyOpaqueReplayDoesNotBlockOrdinaryWorldLoadOrCurrentResave)
{
	historicalCapture = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(expectHistoricalCapture(*gameState()));
	gameState()->replayLog = ReplayLog();
	gameState()->replayLog.beginDay(gameState()->day, {std::byte{7}});
	gameState()->replayLog.addPack({std::byte{8}});
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES;
	old.oser & *gameState();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadReplaySnapshot(ReplayPayload{old.extractBuffer(), old.oser.version});
	ASSERT_NO_FATAL_FAILURE(expectHistoricalCapture(restored));
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
