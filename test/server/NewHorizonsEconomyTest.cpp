/*
 * NewHorizonsEconomyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
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

		if(overrideTaxCollectorPerkRules)
		{
			JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
			if(taxCollectorIsPlanned)
			{
				for(auto & perk : perkRules["skills"]["new-horizons:estates"]["perks"].Vector())
					if(perk["id"].String() == "new-horizons:estates.taxCollector")
						perk["effect"]["status"].String() = "planned";
			}
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		}
		if(overrideEstateNetworkPerkRules)
		{
			JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
			if(estateNetworkIsPlanned)
			{
				for(auto & perk : perkRules["skills"]["new-horizons:estates"]["perks"].Vector())
					if(perk["id"].String() == "new-horizons:estates.estateNetwork")
						perk["effect"]["status"].String() = "planned";
			}
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		}
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

	void startTaxCollectorGame(size_t townCount, bool planned = false)
	{
		useNewHorizonsRules = true;
		overrideTaxCollectorPerkRules = true;
		taxCollectorIsPlanned = planned;

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0));
		for(size_t index = 0; index < townCount; ++index)
		{
			const int x = 4 + static_cast<int>(index % 4) * 7;
			const int y = 4 + static_cast<int>(index / 4) * 7;
			builder.town({x, y, 0}, FactionID::RAMPART, PlayerColor(0));
		}
		builder.hero({32, 32, 0}, HeroTypeID(0), PlayerColor(0));
		startWithMap(std::move(builder));

		towns = findAll<CGTownInstance>();
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_EQ(towns.size(), townCount);
		ASSERT_NE(hero, nullptr);
	}

	void setEstatesRank(int rank)
	{
		setEstatesRank(hero, rank);
	}

	void setEstatesRank(CGHeroInstance * target, int rank)
	{
		const int decoded = SecondarySkill::decode("new-horizons:estates");
		ASSERT_GE(decoded, 0);
		target->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
	}

	void startEstateNetworkGame(size_t townCount, bool planned = false)
	{
		useNewHorizonsRules = true;
		overrideEstateNetworkPerkRules = true;
		estateNetworkIsPlanned = planned;

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1));
		for(size_t index = 0; index < townCount; ++index)
			builder.town({4 + static_cast<int>(index % 4) * 7, 4 + static_cast<int>(index / 4) * 7, 0},
				FactionID::RAMPART, PlayerColor(0));
		builder.hero({32, 32, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({32, 28, 0}, HeroTypeID(1), PlayerColor(0));
		startWithMap(std::move(builder));

		towns = findAll<CGTownInstance>();
		hero = findHeroAt({32, 32, 0});
		secondHero = findHeroAt({32, 28, 0});
		ASSERT_EQ(towns.size(), townCount);
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(secondHero, nullptr);
	}

	void selectEstateNetwork(CGHeroInstance * target)
	{
		setEstatesRank(target, 2);
		target->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
		target->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.estateNetwork"});
	}

	void setOwnedTownCount(CGameHandler & handler, size_t ownedTownCount)
	{
		for(size_t index = 0; index < towns.size(); ++index)
		{
			const PlayerColor newOwner = index < ownedTownCount ? PlayerColor(0) : PlayerColor(1);
			if(towns[index]->getOwner() != newOwner)
				handler.setOwner(towns[index], newOwner);
		}
	}

	void advanceToNextWeekStart(CGameHandler & handler, RecordingGameServer & server)
	{
		while(gameState()->day % 7 != 0)
		{
			handler.onNewTurn();
			ASSERT_TRUE(server.lastNewTurn);
			const auto & income = server.lastNewTurn->playerIncome.at(PlayerColor(0));
			EXPECT_EQ(income[EGameResID::WOOD], 0);
			EXPECT_EQ(income[EGameResID::ORE], 0);
		}
		handler.onNewTurn();
	}

	static int playerIncome(const NewTurn & turn, PlayerColor player, EGameResID resource)
	{
		const auto it = turn.playerIncome.find(player);
		return it == turn.playerIncome.end() ? 0 : it->second[resource];
	}

	void selectTaxCollector(int rank = MasteryLevel::BASIC)
	{
		setEstatesRank(rank);
		hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	}

	int ownedTownIncome() const
	{
		int result = 0;
		for(const auto * town : gameState()->getPlayerState(PlayerColor(0))->getTowns())
			result += town->dailyIncome()[EGameResID::GOLD];
		return result;
	}

	void expectFirstDailyIncomePack(int expectedTaxGold)
	{
		const int expectedHeroIncome = 125 + expectedTaxGold;
		EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], expectedHeroIncome);
		const int expectedIncome = ownedTownIncome() + expectedHeroIncome;
		const int goldBefore = gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD];

		RecordingGameServer server(gameState());
		CGameHandler handler(server, gameState());
		handler.onNewTurn(); // Initial turn does not pay daily income.
		handler.onNewTurn();

		ASSERT_TRUE(server.lastNewTurn);
		EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::GOLD], expectedIncome);
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], goldBefore + expectedIncome);
	}

	bool useNewHorizonsRules = false;
	bool overrideTaxCollectorPerkRules = false;
	bool taxCollectorIsPlanned = false;
	bool overrideEstateNetworkPerkRules = false;
	bool estateNetworkIsPlanned = false;
	std::vector<CGTownInstance *> towns;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * secondHero = nullptr;
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

TEST_F(NewHorizonsEconomyTest, TaxCollectorAddsFiftyGoldPerOwnedTown)
{
	startTaxCollectorGame(1);
	selectTaxCollector();

	ASSERT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	expectFirstDailyIncomePack(50);
}

TEST_F(NewHorizonsEconomyTest, TaxCollectorIncomeCapsAtTenOwnedTowns)
{
	startTaxCollectorGame(10);
	selectTaxCollector();

	ASSERT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	expectFirstDailyIncomePack(500);
}

TEST_F(NewHorizonsEconomyTest, TaxCollectorIncomeRemainsCappedAboveTenOwnedTowns)
{
	startTaxCollectorGame(11);
	selectTaxCollector();

	ASSERT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	expectFirstDailyIncomePack(500);
}

TEST_F(NewHorizonsEconomyTest, TaxCollectorProducesNoGoldWithoutOwnedTowns)
{
	startTaxCollectorGame(0);
	selectTaxCollector();

	ASSERT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	expectFirstDailyIncomePack(0);
}

TEST_F(NewHorizonsEconomyTest, TaxCollectorUsesCurrentTownOwnershipForTheNextDailyPack)
{
	startTaxCollectorGame(2);
	selectTaxCollector();

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	const int goldBefore = gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD];
	const int firstExpectedIncome = ownedTownIncome() + 125 + 100;
	handler.onNewTurn();
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::GOLD], firstExpectedIncome);

	const int goldAfterFirstPack = gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD];
	handler.setOwner(towns.back(), PlayerColor::NEUTRAL);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->getTowns().size(), 1u);
	const int secondExpectedIncome = ownedTownIncome() + 125 + 50;
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::GOLD], secondExpectedIncome);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], goldAfterFirstPack + secondExpectedIncome);
	EXPECT_EQ(goldAfterFirstPack, goldBefore + firstExpectedIncome);
}

TEST_F(NewHorizonsEconomyTest, TaxCollectorRequiresAnActiveSelectedPerkAndBasicRank)
{
	startTaxCollectorGame(1);
	setEstatesRank(MasteryLevel::BASIC);
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 125);

	hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	setEstatesRank(MasteryLevel::NONE);
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 0);
}

TEST_F(NewHorizonsEconomyTest, PlannedTaxCollectorCannotBeSelectedOrAffectIncome)
{
	startTaxCollectorGame(1, true);
	setEstatesRank(MasteryLevel::BASIC);
	EXPECT_THROW(hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"}), std::runtime_error);
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 125);
}

TEST_F(NewHorizonsEconomyTest, AdvancedEstatesCanSelectTheBasicTaxCollectorPerk)
{
	startTaxCollectorGame(1);
	selectTaxCollector(MasteryLevel::ADVANCED);

	EXPECT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	EXPECT_EQ(hero->dailyIncome()[EGameResID::GOLD], 300);
}

TEST_F(NewHorizonsEconomyTest, EstateNetworkPaysFloorMinimumPerActiveHolderAtEachWeekStart)
{
	startEstateNetworkGame(6);
	selectEstateNetwork(hero);
	setEstatesRank(secondHero, 2);
	secondHero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	towns.front()->setGarrisonedHero(secondHero);
	ASSERT_EQ(towns.front()->getGarrisonHero(), secondHero);
	EXPECT_FALSE(secondHero->hasActivePerk("new-horizons:estates", "new-horizons:estates.estateNetwork"));

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.onNewTurn(); // Day zero -> one is the first week start.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 1u);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 2);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 2);

	// A garrisoned hero is still an owned Estate Network holder. Town ownership
	// is recalculated at each week boundary, and each active holder contributes
	// the same town-count-based amount.
	setOwnedTownCount(handler, 5);
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 8u);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 1);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 1);

	setOwnedTownCount(handler, 3);
	secondHero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.estateNetwork"});
	ASSERT_TRUE(secondHero->hasActivePerk("new-horizons:estates", "new-horizons:estates.estateNetwork"));
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 15u);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 2);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 2);

	setOwnedTownCount(handler, 2);
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 2);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 2);

	setOwnedTownCount(handler, 1);
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 2);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 2);

	// No town owned means the minimum-one clause does not apply.
	towns.front()->setGarrisonedHero(nullptr);
	setOwnedTownCount(handler, 0);
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 0);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 0);

	// Restore six towns and two eligible heroes to establish per-holder stacking.
	setOwnedTownCount(handler, 6);
	towns.front()->setGarrisonedHero(secondHero);
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 4);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 4);
}

TEST_F(NewHorizonsEconomyTest, EstateNetworkRequiresAdvancedRankAndAnActiveSelectedPerk)
{
	startEstateNetworkGame(1);
	setEstatesRank(hero, 1);
	hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.estateNetwork"));
	EXPECT_THROW(hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.estateNetwork"}), std::runtime_error);

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(playerIncome(*server.lastNewTurn, PlayerColor(0), EGameResID::WOOD), 0);
	EXPECT_EQ(playerIncome(*server.lastNewTurn, PlayerColor(0), EGameResID::ORE), 0);

	setEstatesRank(hero, 2);
	hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.estateNetwork"});
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.estateNetwork"));
	advanceToNextWeekStart(handler, server);
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 1);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 1);
}

TEST_F(NewHorizonsEconomyTest, PlannedEstateNetworkCannotBeSelectedOrGrantResources)
{
	startEstateNetworkGame(1, true);
	setEstatesRank(hero, 2);
	hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	EXPECT_THROW(hero->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.estateNetwork"}), std::runtime_error);
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.estateNetwork"));

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(playerIncome(*server.lastNewTurn, PlayerColor(0), EGameResID::WOOD), 0);
	EXPECT_EQ(playerIncome(*server.lastNewTurn, PlayerColor(0), EGameResID::ORE), 0);
}

TEST_F(NewHorizonsEconomyTest, EstateNetworkDayOneGrantRespectsResourceCapAndDoesNotRepeatAfterSaveLoad)
{
	startEstateNetworkGame(6);
	selectEstateNetwork(hero);
	const auto startingWood = GameConstants::PLAYER_RESOURCES_CAP - 1;
	gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::WOOD] = startingWood;

	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 2);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::WOOD], GameConstants::PLAYER_RESOURCES_CAP);
	EXPECT_EQ(server.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 2);

	const auto saved = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(saved);
	ASSERT_EQ(restored->day, 1u);
	ASSERT_TRUE(restored->getHero(hero->id)->getPerkState().hasSelection(
		"new-horizons:estates", "new-horizons:estates.estateNetwork"));

	RecordingGameServer restoredServer(restored);
	CGameHandler restoredHandler(restoredServer, restored);
	restoredHandler.onNewTurn(); // Day two is ordinary income, not another weekly trigger.
	ASSERT_TRUE(restoredServer.lastNewTurn);
	EXPECT_EQ(restoredServer.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::WOOD], 0);
	EXPECT_EQ(restoredServer.lastNewTurn->playerIncome.at(PlayerColor(0))[EGameResID::ORE], 0);
	EXPECT_EQ(restored->getPlayerState(PlayerColor(0))->resources[EGameResID::WOOD], GameConstants::PLAYER_RESOURCES_CAP);
}
