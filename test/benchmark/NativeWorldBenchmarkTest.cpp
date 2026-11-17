/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEncounterBenchmarkSupport.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/network/InitializationProfile.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"

namespace
{
using namespace nativeBenchmark;
class DISABLED_NativeWorldBenchmarkTest : public TinyMapGameTest
{
protected:
	LogScope logging;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	void configurePlayer(PlayerSettings & settings) const override { settings.bonus = PlayerStartingBonus::GOLD; }
	void prepare(int side)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(side, false).name("Owned native world benchmark")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 14}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(CreatureID::decode("core:archer")), 9}})
			.town({15, 15, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
			.town({25, 25, 0}, FactionID::RAMPART, PlayerColor(1)).townGarrison({})
			.monster({12, 6, 0}, CreatureID(CreatureID::decode("core:halfling")), 29);
		{
			nhLoadProfile::Phase phase("native-benchmark-initialize");
			RecordProperty("initializationProfilingActive", nhLoadProfile::active() ? "1" : "0");
			const auto started = Clock::now();
			ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder), EMapDifficulty::NORMAL));
			RecordProperty("initializationUs", std::to_string(microseconds(started)));
		}
		RecordProperty("mapSide", std::to_string(side));
		RecordProperty("seed", "1337");
		RecordProperty("initialEvidence", worldEvidence(*gameState()).toString());
		RecordProperty("scope", "Owned generated H3M parse and world init; excludes GUI and ordinary lobby; assets external");
		ASSERT_EQ(map()->width, side);
		ASSERT_EQ(map()->height, side);
		ASSERT_EQ(map()->getObjects<CGHeroInstance>().size(), 2u);
		for(const auto * hero : map()->getObjects<CGHeroInstance>())
			ASSERT_TRUE(hero->getPerkState().toJson()["selected"].Vector().empty());
	}
	static JsonNode worldEvidence(const CGameState & state)
	{
		JsonNode result;
		result["day"].Integer() = state.day;
		result["width"].Integer() = state.getMap().width;
		result["height"].Integer() = state.getMap().height;
		for(const auto * hero : state.getMap().getObjects<CGHeroInstance>())
		{
			JsonNode row = heroEvidence(*hero);
			row["id"].Integer() = hero->id.getNum();
			row["army"] = armyEvidence(*hero);
			row["x"].Integer() = hero->pos.x;
			row["y"].Integer() = hero->pos.y;
			row["movement"].Integer() = hero->movementPointsRemaining();
			row["experience"].Integer() = hero->exp;
			result["heroes"].Vector().push_back(std::move(row));
		}
		for(const auto * monster : state.getMap().getObjects<CGCreature>())
			result["neutrals"].Vector().push_back(armyEvidence(*monster));
		for(const auto * town : state.getMap().getObjects<CGTownInstance>())
		{
			JsonNode row;
			row["id"].Integer() = town->id.getNum();
			row["army"] = armyEvidence(*town);
			row["growthHex"].String() = hex(bytes(town->creatures));
			result["towns"].Vector().push_back(std::move(row));
		}
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
			result["resourcesHex"].Vector().emplace_back(hex(bytes(state.getPlayerState(player)->resources)));
		return result;
	}
	void dayEightRoundTrip()
	{
		ASSERT_NO_FATAL_FAILURE(prepare(36));
		GameHandlerTestServer server(gameState());
		CGameHandler handler(server, gameState());
		handler.randomizer->setSeed(1337);
		CRandomGenerator::getDefault().setSeed(1337);
		// Advance real authoritative day/economy/growth boundaries, not a
		// synthetic assignment to CGameState::day. No battles are in flight.
		for(int turn = 0; gameState()->day < 8 && turn < 8; ++turn)
			NewTurnProcessor(&handler).onNewTurn();
		ASSERT_EQ(gameState()->day, 8u);
		ASSERT_TRUE(gameState()->currentBattles.empty());
		const auto before = worldEvidence(*gameState());
		const auto aiRng = bytes(CRandomGenerator::getDefault());
		RecordProperty("aiRngBefore", hex(aiRng));
		const auto rng = bytes(*handler.randomizer);
		const auto writeStart = Clock::now();
		auto saved = gameState()->saveToMemory();
		const auto writeUs = microseconds(writeStart);
		RecordProperty("snapshotBytes", std::to_string(saved.size()));
		RecordProperty("authorityRngBytes", std::to_string(rng.size()));
		RecordProperty("snapshotWriteUs", std::to_string(writeUs));
		auto restored = std::make_shared<CGameState>();
		restored->preInit(LIBRARY);
		const auto readStart = Clock::now();
		restored->loadFromMemory(std::move(saved));
		RecordProperty("snapshotReadUs", std::to_string(microseconds(readStart)));
		ASSERT_EQ(worldEvidence(*restored), before);
		GameHandlerTestServer restoredServer(restored);
		CGameHandler restoredHandler(restoredServer, restored);
		CMemorySerializer rngCopy(rng);
		rngCopy.iser & *restoredHandler.randomizer;
		ASSERT_EQ(bytes(*restoredHandler.randomizer), rng);
		ASSERT_EQ(bytes(CRandomGenerator::getDefault()), aiRng);
		RecordProperty("restoredEvidence", worldEvidence(*restored).toString());
		RecordProperty("authorityRngBefore", hex(rng));
		// A same-format memory snapshot omits replay history and the handler.
		// Its RNG envelope is explicit here; this is not a disk-save claim.
		NewTurnProcessor(&handler).onNewTurn();
		NewTurnProcessor(&restoredHandler).onNewTurn();
		ASSERT_EQ(worldEvidence(*gameState()), worldEvidence(*restored));
		ASSERT_EQ(bytes(*handler.randomizer), bytes(*restoredHandler.randomizer));
		RecordProperty("continuedEvidence", worldEvidence(*restored).toString());
		RecordProperty("authorityRngAfter", hex(bytes(*restoredHandler.randomizer)));
		RecordProperty("aiRngAfter", hex(bytes(CRandomGenerator::getDefault())));
		RecordProperty("persistenceScope", "Current-format day8 world snapshot + explicit authority RNG envelope, day9 continuation; no disk/lobby/replay-history acceptance");
	}
};
TEST_F(DISABLED_NativeWorldBenchmarkTest, SmallMapInitialization)
{
	prepare(36);
}
TEST_F(DISABLED_NativeWorldBenchmarkTest, LargeMapInitialization)
{
	prepare(108);
}
TEST_F(DISABLED_NativeWorldBenchmarkTest, DayEightCurrentFormatMemoryRoundTrip)
{
	dayEightRoundTrip();
}
}
