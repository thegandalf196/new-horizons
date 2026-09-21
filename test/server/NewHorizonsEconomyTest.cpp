/*
 * NewHorizonsEconomyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../../server/processors/NewTurnProcessor.h"
#include "../mock/TinyMapGameTest.h"

namespace
{

class RecordingGameServer final : public IGameServer
{
public:
	explicit RecordingGameServer(std::shared_ptr<CGameState> gameState)
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
		gameState->apply(pack);
	}

	void sendPack(CPackForClient &, GameConnectionID) override {}

	std::optional<NewTurn> lastNewTurn;

private:
	EServerState state = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> gameState;
};

class NewHorizonsEconomyTest : public TinyMapGameTest
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

		JsonNode daysPerWeek;
		daysPerWeek.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, daysPerWeek);
		JsonNode weeksPerMonth;
		weeksPerMonth.Integer() = 4;
		loaded->overrideGameSetting(EGameSettings::GENERAL_WEEKS_PER_MONTH, weeksPerMonth);

		if(useNewHorizonsRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
				JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		else
			// The module enables New Horizons globally. An explicit null map
			// override is the saved-world contract for exercising legacy behavior.
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	void startRampartGame(bool newHorizons, int townCount)
	{
		useNewHorizonsRules = newHorizons;

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0));
		for(int index = 0; index < townCount; ++index)
			builder.town({5 + index * 8, 5, 0}, FactionID::RAMPART, PlayerColor(0));
		startWithMap(std::move(builder));

		towns = findAll<CGTownInstance>();
		ASSERT_EQ(towns.size(), static_cast<size_t>(townCount));
	}

	void setGoldBeforeWeek(int amount)
	{
		const auto current = gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD];
		grantResources(PlayerColor(0), GameResID(EGameResID::GOLD), amount - current);
	}

	void reachWeekStart(CGameHandler & handler, RecordingGameServer & server, int gold)
	{
		ASSERT_EQ(gameState()->day, 0u);
		// Day zero is the initial setup turn. Advance through all seven days of
		// week one, then generate day eight (the first day of week two).
		for(int day = 0; day < 7; ++day)
			handler.onNewTurn();

		setGoldBeforeWeek(gold);
		handler.onNewTurn();
		EXPECT_EQ(gameState()->day, 8u);
		EXPECT_TRUE(server.lastNewTurn.has_value());
	}

	bool useNewHorizonsRules = false;
	std::vector<CGTownInstance *> towns;
};

bool isPrecious(GameResID resource)
{
	return resource == GameResID(EGameResID::MERCURY)
		|| resource == GameResID(EGameResID::SULFUR)
		|| resource == GameResID(EGameResID::CRYSTAL)
		|| resource == GameResID(EGameResID::GEMS);
}

} // namespace

TEST(NewHorizonsEconomyRulesTest, TreasuryInterestCapsPerBuilding)
{
	EXPECT_EQ(newHorizonsEconomy::treasuryWeeklyIncome(20000), 2000);
	EXPECT_EQ(newHorizonsEconomy::treasuryWeeklyIncome(50000), 2000);
}

TEST(NewHorizonsEconomyRulesTest, TreasuryInterestUsesCurrentGoldBelowCap)
{
	EXPECT_EQ(newHorizonsEconomy::treasuryWeeklyIncome(19999), 1999);
	EXPECT_EQ(newHorizonsEconomy::treasuryWeeklyIncome(0), 0);
}

TEST(NewHorizonsEconomyRulesTest, MysticPondResultsRoundTripAndAreNotSilentlyDiscarded)
{
	NewTurn source;
	source.newHorizonsMysticPondResults[ObjectInstanceID(17)] = {
		GameResID(EGameResID::MERCURY), GameResID(EGameResID::GEMS)
	};

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & source);
	NewTurn restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.newHorizonsMysticPondResults, source.newHorizonsMysticPondResults);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PERFECT_MOMENT;
	EXPECT_THROW(old.oser & source, std::runtime_error);
}

TEST_F(NewHorizonsEconomyTest, MultipleTreasuriesAcrossTownsEachReceiveCappedInterest)
{
	startRampartGame(true, 2);
	for(auto * town : towns)
		town->addBuilding(BuildingID::SPECIAL_3);

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	reachWeekStart(handler, server, 50000);

	ASSERT_TRUE(server.lastNewTurn);
	const auto & income = server.lastNewTurn->playerIncome.at(PlayerColor(0));
	// Both towns retain their normal 500-gold daily income in addition to the
	// two independently capped 2,000-gold Treasury payments.
	EXPECT_EQ(income[EGameResID::GOLD], 5000);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], 55000);
}

TEST_F(NewHorizonsEconomyTest, MysticPondAddsExactlyTwoPreciousResourcesToNormalState)
{
	startRampartGame(true, 1);
	towns.front()->addBuilding(BuildingID::SPECIAL_1);
	const auto resourcesBefore = gameState()->getPlayerState(PlayerColor(0))->resources;

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	reachWeekStart(handler, server, 0);

	ASSERT_TRUE(server.lastNewTurn);
	const auto & pondResult = server.lastNewTurn->newHorizonsMysticPondResults.at(towns.front()->id);
	EXPECT_EQ(pondResult.size(), static_cast<size_t>(newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT));
	EXPECT_EQ(towns.front()->newHorizonsMysticPondResources, pondResult);
	const auto & income = server.lastNewTurn->playerIncome.at(PlayerColor(0));
	int preciousTotal = 0;
	for(const auto resource : {GameResID(EGameResID::MERCURY), GameResID(EGameResID::SULFUR),
		GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS)})
	{
		EXPECT_GE(income[resource], 0);
		EXPECT_LE(income[resource], newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT);
		preciousTotal += income[resource];
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[resource], resourcesBefore[resource] + income[resource]);
	}
	EXPECT_EQ(preciousTotal, newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT);
	EXPECT_EQ(income[EGameResID::WOOD], 0);
	EXPECT_EQ(income[EGameResID::ORE], 0);
	EXPECT_EQ(income[EGameResID::GOLD], 500);
}

TEST_F(NewHorizonsEconomyTest, MysticPondResultSurvivesSaveAndLoad)
{
	startRampartGame(true, 1);
	towns.front()->addBuilding(BuildingID::SPECIAL_1);

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	reachWeekStart(handler, server, 0);

	const auto townID = towns.front()->id;
	const auto expected = towns.front()->newHorizonsMysticPondResources;
	ASSERT_EQ(expected.size(), static_cast<size_t>(newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	ASSERT_NE(restored.getTown(townID), nullptr);
	EXPECT_EQ(restored.getTown(townID)->newHorizonsMysticPondResources, expected);
}

TEST_F(NewHorizonsEconomyTest, LegacyTreasuryAndMysticPondRemainUnchanged)
{
	startRampartGame(false, 1);
	towns.front()->addBuilding(BuildingID::SPECIAL_1);
	towns.front()->addBuilding(BuildingID::SPECIAL_3);

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	reachWeekStart(handler, server, 50000);

	ASSERT_TRUE(server.lastNewTurn);
	const auto & income = server.lastNewTurn->playerIncome.at(PlayerColor(0));
	EXPECT_EQ(income[EGameResID::GOLD], 5500);
	int preciousTotal = 0;
	int preciousTypes = 0;
	for(const auto resource : {GameResID(EGameResID::MERCURY), GameResID(EGameResID::SULFUR),
		GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS)})
	{
		preciousTotal += income[resource];
		preciousTypes += income[resource] != 0;
	}
	EXPECT_GE(preciousTotal, 1);
	EXPECT_LE(preciousTotal, 4);
	EXPECT_EQ(preciousTypes, 1);
	EXPECT_EQ(towns.front()->bonusValue.second, preciousTotal);
	EXPECT_TRUE(isPrecious(GameResID(towns.front()->bonusValue.first)));
	EXPECT_TRUE(towns.front()->newHorizonsMysticPondResources.empty());
}
