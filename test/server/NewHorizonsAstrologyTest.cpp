/*
 * NewHorizonsAstrologyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 */
#include "StdInc.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/gameState/NewHorizonsAstrology.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../mock/TinyMapGameTest.h"

#include <algorithm>
#include <optional>

namespace
{

class AstrologyRecordingServer final : public IGameServer
{
public:
	explicit AstrologyRecordingServer(std::shared_ptr<CGameState> gameState)
		: gameState(std::move(gameState))
	{}

	void setState(EServerState value) override { state = value; }
	EServerState getState() const override { return state; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * turn = dynamic_cast<const NewTurn *>(&pack))
			lastNewTurn = *turn;
		if(const auto * structures = dynamic_cast<const NewStructures *>(&pack))
			lastNewStructures = *structures;
		gameState->apply(pack);
	}

	void sendPack(CPackForClient &, GameConnectionID) override {}

	std::optional<NewTurn> lastNewTurn;
	std::optional<NewStructures> lastNewStructures;

private:
	EServerState state = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> gameState;
};

class NewHorizonsAstrologyGameTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode daysPerWeek;
		daysPerWeek.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, daysPerWeek);
		JsonNode weeksPerMonth;
		weeksPerMonth.Integer() = 4;
		loaded->overrideGameSetting(EGameSettings::GENERAL_WEEKS_PER_MONTH, weeksPerMonth);

		JsonNode randomWeeks;
		randomWeeks.Bool() = true;
		loaded->overrideGameSetting(EGameSettings::CREATURES_ALLOW_RANDOM_SPECIAL_WEEKS, randomWeeks);
		JsonNode specialWeekProbability;
		specialWeekProbability.Integer() = 100;
		loaded->overrideGameSetting(EGameSettings::CREATURES_WEEK_SPECIAL_PROBABILITY, specialWeekProbability);
		JsonNode additionalGrowth;
		additionalGrowth.Integer() = 5;
		loaded->overrideGameSetting(EGameSettings::CREATURES_ADDITIONAL_WEEKLY_GROWTH_SPECIAL_WEEK, additionalGrowth);
	}

	CGTownInstance * startTowerTown()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({18, 18, 0}, FactionID::TOWER, PlayerColor(0));
		startWithMap(std::move(builder));
		return expectAt<CGTownInstance>({18, 18, 0});
	}
};

AstrologyWeek rollNonMonthAstrologyWeek(CGameHandler & handler)
{
	// All principal-path cases below use week two and four-week months. This
	// mirrors NewTurnProcessor's non-month pick: one probability draw followed
	// by the same creature eligibility/retry loop.
	static_cast<void>(handler.getRandomGenerator().nextInt(99));
	CreatureID creature;
	do
	{
		creature = handler.randomizer->rollCreature();
	} while(creature.toEntity(LIBRARY)->getFactionID().toFaction()->town == nullptr);
	return {EWeekType::BONUS_GROWTH, creature, 5};
}

struct TowerForecastSeed
{
	int seed = 0;
	size_t dwellingLevel = 0;
	AstrologyWeek upcoming;
	AstrologyWeek following;
};

std::optional<TowerForecastSeed> findTowerForecastSeed(CGameHandler & handler, const CGTownInstance & town)
{
	const auto & towerCreatures = town.getTown()->creatures;
	for(int seed = 1; seed <= 10000; ++seed)
	{
		handler.randomizer->setSeed(seed);
		const auto upcoming = rollNonMonthAstrologyWeek(handler);
		const auto following = rollNonMonthAstrologyWeek(handler);
		if(upcoming.creature == following.creature)
			continue;

		for(size_t level = 0; level < towerCreatures.size(); ++level)
			if(!towerCreatures[level].empty() && towerCreatures[level].front() == upcoming.creature)
				return TowerForecastSeed{seed, level, upcoming, following};
	}
	return std::nullopt;
}

void assertWeekAppliedAndForecastAdvanced(
	AstrologyRecordingServer & server,
	const CGTownInstance & town,
	const AstrologyWeek & expectedCurrent,
	const std::optional<AstrologyWeek> & expectedNext,
	size_t dwellingLevel,
	const std::vector<std::pair<ui32, std::vector<CreatureID>>> & stockBefore)
{
	ASSERT_TRUE(server.lastNewTurn.has_value());
	const auto & turn = *server.lastNewTurn;
	EXPECT_EQ(turn.specialWeek, expectedCurrent.type);
	EXPECT_EQ(turn.creatureid, expectedCurrent.creature);
	EXPECT_TRUE(turn.nextAstrologyWeek.known());
	if(expectedNext)
	{
		EXPECT_EQ(turn.nextAstrologyWeek, *expectedNext);
		EXPECT_NE(turn.nextAstrologyWeek, expectedCurrent);
	}

	ASSERT_LT(dwellingLevel, town.creatures.size());
	ASSERT_FALSE(town.creatures.at(dwellingLevel).second.empty());
	ASSERT_EQ(town.creatures.at(dwellingLevel).second.back(), expectedCurrent.creature);
	ASSERT_GT(expectedCurrent.additionalGrowth, 0);

	const auto growth = std::find_if(turn.availableCreatures.begin(), turn.availableCreatures.end(), [&town](const SetAvailableCreatures & update)
	{
		return update.tid == town.id;
	});
	ASSERT_NE(growth, turn.availableCreatures.end());
	ASSERT_LT(dwellingLevel, growth->creatures.size());
	ASSERT_LT(dwellingLevel, stockBefore.size());
	const auto before = stockBefore.at(dwellingLevel).first;
	const auto regularGrowth = town.creatureGrowth(static_cast<int>(dwellingLevel));
	EXPECT_EQ(growth->creatures.at(dwellingLevel).first,
		before + regularGrowth + expectedCurrent.additionalGrowth);
	EXPECT_EQ(town.creatures.at(dwellingLevel).first, growth->creatures.at(dwellingLevel).first);
}

} // namespace

TEST(NewHorizonsAstrologyWire, PreviewResultRoundTripsWithTheNewSaveFeature)
{
	NewTurn outgoing;
	outgoing.nextAstrologyWeek = {
		EWeekType::BONUS_GROWTH,
		CreatureID(3),
		7
	};

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	NewTurn incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.nextAstrologyWeek, outgoing.nextAstrologyWeek);
}

TEST(NewHorizonsAstrologyWire, OlderFormatsRejectAnAuthoredPreviewInsteadOfDiscardingIt)
{
	NewTurn outgoing;
	outgoing.nextAstrologyWeek = {
		EWeekType::PLAGUE,
		CreatureID::NONE,
		0
	};

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MYSTIC_POND_RESULTS;
	EXPECT_THROW(old.oser & outgoing, std::runtime_error);
}

TEST(NewHorizonsAstrologyWire, ConstructionPreviewRoundTripsAndOlderFormatsRemainEmpty)
{
	NewStructures outgoing;
	outgoing.tid = ObjectInstanceID(42);
	outgoing.bid.insert(BuildingID::SPECIAL_2);
	outgoing.built = 3;
	outgoing.nextAstrologyWeek = {EWeekType::BONUS_GROWTH, CreatureID(3), 5};

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);

	NewStructures restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.tid, outgoing.tid);
	EXPECT_EQ(restored.bid, outgoing.bid);
	EXPECT_EQ(restored.built, outgoing.built);
	ASSERT_TRUE(restored.nextAstrologyWeek.has_value());
	EXPECT_EQ(*restored.nextAstrologyWeek, *outgoing.nextAstrologyWeek);

	const auto previousFormat = ESerializationVersion::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP;
	NewStructures unsupported = outgoing;
	CMemorySerializer oldWriter;
	oldWriter.oser.version = previousFormat;
	EXPECT_THROW(oldWriter.oser & unsupported, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	NewStructures legacyOutgoing;
	legacyOutgoing.tid = ObjectInstanceID(7);
	legacyOutgoing.bid.insert(BuildingID::SPECIAL_2);
	legacyOutgoing.built = 1;
	CMemorySerializer legacy;
	legacy.oser.version = previousFormat;
	legacy.iser.version = previousFormat;
	ASSERT_NO_THROW(legacy.oser & legacyOutgoing);
	NewStructures legacyRestored;
	ASSERT_NO_THROW(legacy.iser & legacyRestored);
	EXPECT_FALSE(legacyRestored.nextAstrologyWeek.has_value());
}

TEST_F(NewHorizonsAstrologyGameTest, TowerBuiltOnLastDayAuthorsTheNextWeekAndAppliesItsGrowth)
{
	auto * town = startTowerTown();
	ASSERT_NE(town, nullptr);
	ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::SPECIAL_2));

	AstrologyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto seed = findTowerForecastSeed(handler, *town);
	ASSERT_TRUE(seed.has_value()) << "could not find a deterministic Tower creature forecast";

	const BuildingID targetDwelling(BuildingID(BuildingID::DWELL_LVL_1).getNum() + static_cast<int>(seed->dwellingLevel));
	ASSERT_TRUE(town->getTown()->buildings.contains(targetDwelling));
	ASSERT_TRUE(handler.buildStructure(town->id, targetDwelling, true));

	for(int turn = 0; turn < 7; ++turn)
		handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 7u);
	EXPECT_FALSE(gameState()->nextAstrologyWeek.known());

	handler.randomizer->setSeed(seed->seed);
	const auto dayBeforeConstruction = gameState()->day;
	ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::SPECIAL_2, true));
	EXPECT_EQ(gameState()->day, dayBeforeConstruction);
	ASSERT_TRUE(town->hasBuilt(BuildingID::SPECIAL_2));
	ASSERT_TRUE(server.lastNewStructures.has_value());
	ASSERT_TRUE(server.lastNewStructures->nextAstrologyWeek.has_value());
	EXPECT_EQ(*server.lastNewStructures->nextAstrologyWeek, seed->upcoming);
	EXPECT_EQ(gameState()->nextAstrologyWeek, seed->upcoming);

	const auto stockBefore = town->creatures;
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8u);
	assertWeekAppliedAndForecastAdvanced(server, *town, seed->upcoming, seed->following, seed->dwellingLevel, stockBefore);
	EXPECT_EQ(gameState()->nextAstrologyWeek, seed->following);
}

TEST_F(NewHorizonsAstrologyGameTest, DayZeroConstructionPreviewSurvivesTheFirstTurn)
{
	auto * town = startTowerTown();
	ASSERT_NE(town, nullptr);
	ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::SPECIAL_2));

	AstrologyRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto seed = findTowerForecastSeed(handler, *town);
	ASSERT_TRUE(seed.has_value()) << "could not find a deterministic Tower creature forecast";

	const BuildingID targetDwelling(BuildingID(BuildingID::DWELL_LVL_1).getNum() + static_cast<int>(seed->dwellingLevel));
	ASSERT_TRUE(town->getTown()->buildings.contains(targetDwelling));
	ASSERT_TRUE(handler.buildStructure(town->id, targetDwelling, true));
	handler.randomizer->setSeed(seed->seed);
	const auto dayBeforeConstruction = gameState()->day;
	ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::SPECIAL_2, true));
	EXPECT_EQ(gameState()->day, dayBeforeConstruction);
	ASSERT_EQ(gameState()->nextAstrologyWeek, seed->upcoming);

	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1u);
	EXPECT_EQ(gameState()->nextAstrologyWeek, seed->upcoming);

	while(gameState()->day < 7)
		handler.onNewTurn();
	EXPECT_EQ(gameState()->nextAstrologyWeek, seed->upcoming);
	const auto stockBefore = town->creatures;
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8u);
	assertWeekAppliedAndForecastAdvanced(server, *town, seed->upcoming, std::nullopt, seed->dwellingLevel, stockBefore);
	EXPECT_TRUE(gameState()->nextAstrologyWeek.known());
}

TEST(NewHorizonsAstrologyState, FirstWeekIsOnlyTheUnknownSentinel)
{
	AstrologyWeek preview;
	EXPECT_FALSE(preview.known());

	preview.type = EWeekType::NORMAL;
	EXPECT_TRUE(preview.known());
}

TEST(NewHorizonsCastleGateWire, DailyUsageRoundTripsToTheClientMirror)
{
	SetNewHorizonsCastleGateState outgoing;
	outgoing.hid = ObjectInstanceID(42);
	outgoing.lastUseDay = 17;

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	SetNewHorizonsCastleGateState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.hid, outgoing.hid);
	EXPECT_EQ(incoming.lastUseDay, outgoing.lastUseDay);
}
