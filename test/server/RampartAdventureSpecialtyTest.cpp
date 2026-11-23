/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleAdventureSpecialtyFixture.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/pathfinder/NewHorizonsElusive.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/GameLibrary.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/queries/CQuery.h"
#include "../../server/queries/QueriesProcessor.h"

namespace
{
using namespace castleSpecialtyTest;

/// Kyrre and Jenova on the shipped rules or on a historical capture without the
/// revised specialty row (`historicalSpecialties`).
class RampartAdventureSpecialtyTest : public CastleSpecialtyFixture
{
protected:
	CGCreature * guard = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;

	void guarded(const char * creatureName, int level = 1)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:kyrre")), PLAYER)
				.heroGarrison({{creature("core:centaur"), 1}})
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
				.heroGarrison({{creature("core:pikeman"), 1}})
			.monster({6, 4, 0}, creature(creatureName), 20, static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));
		specialist = findHeroAt({5, 5, 0});
		ordinary = findHeroAt({24, 24, 0});
		guard = findFirst<CGCreature>();
		ASSERT_TRUE(specialist && ordinary && guard);
		guard->agression = 10;
		guard->neverFlees = true;
		specialist->level = level;
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
	}

	/// A free tile next to Kyrre that the guard protects.
	std::optional<int3> approach() const
	{
		const auto source = specialist->visitablePos();
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const auto tile = source + int3(dx, dy, 0);
				if((dx || dy) && map()->isInTheMap(tile) && tile != guard->visitablePos()
					&& map()->guardingCreaturePosition(tile) == guard->visitablePos() && !map()->getTile(tile).blocked())
					return tile;
			}
		return std::nullopt;
	}

	bool stopsAt(const CGHeroInstance * hero, const int3 & tile) const
	{
		return !gameState()->getGuardingCreatures(tile, hero).empty();
	}

	int gold() const
	{
		return gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	}

	/// Jenova beside the entrance of a town of `owner`; player 1 keeps a town of its own,
	/// so a capture does not end the game.
	void besideTown(PlayerColor owner, std::vector<std::pair<CreatureID, uint16_t>> garrison)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::RAMPART, owner).townGarrison(std::move(garrison))
			.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
			.hero({20, 20, 0}, HeroTypeID(HeroTypeID::decode("core:jenova")), PLAYER)
				.heroGarrison({{creature("core:centaur"), 5}});
		startWithMap(std::move(builder));
		specialist = findHeroAt({20, 20, 0});
		town = expectAt<CGTownInstance>({12, 12, 0});
		ASSERT_TRUE(specialist && town);
		map()->moveObject(specialist->id, town->visitablePos() + int3(0, 1, 0) + specialist->getVisitableOffset());
		ASSERT_EQ(specialist->visitablePos(), town->visitablePos() + int3(0, 1, 0));
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		handler->setMovePoints(specialist->id, 20000);
	}

	/// Level-1 Patroness: 25% of the town's daily income.
	int expectedCollection() const
	{
		return town->dailyIncome()[EGameResID::GOLD] * 250 / 1000;
	}

	void leave()
	{
		HeroVisitCastle exit;
		exit.hid = specialist->id;
		exit.tid = town->id;
		exit.startVisit = false;
		handler->sendAndApply(exit);
	}
};

class HistoricalRampartAdventureTest : public RampartAdventureSpecialtyTest
{
protected:
	HistoricalRampartAdventureTest()
	{
		historicalSpecialties = true;
	}
};
}

TEST_F(RampartAdventureSpecialtyTest, ElusiveKyrrePassesBesideACoreGuardOthersStop)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:pikeman"));
	const auto tile = approach();
	ASSERT_TRUE(tile);
	EXPECT_FALSE(stopsAt(specialist, *tile));
	EXPECT_TRUE(stopsAt(ordinary, *tile)) << "Elusive is Kyrre's alone";
	EXPECT_TRUE(stopsAt(specialist, guard->visitablePos())) << "The guard's own tile still means a fight";
}

TEST_F(RampartAdventureSpecialtyTest, ElusiveCoversEliteGuardsFromLevelTen)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:griffin"));
	const auto tile = approach();
	ASSERT_TRUE(tile);
	EXPECT_TRUE(stopsAt(specialist, *tile));
	specialist->level = 9;
	EXPECT_TRUE(stopsAt(specialist, *tile));
	specialist->level = 10;
	EXPECT_FALSE(stopsAt(specialist, *tile));
}

TEST_F(RampartAdventureSpecialtyTest, ElusiveCoversChampionGuardsFromLevelTwenty)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:angel", 19));
	const auto tile = approach();
	ASSERT_TRUE(tile);
	EXPECT_TRUE(stopsAt(specialist, *tile));
	specialist->level = 20;
	EXPECT_FALSE(stopsAt(specialist, *tile));
}

TEST_F(RampartAdventureSpecialtyTest, UnrankedNeutralsRankByLevel)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:peasant"));
	EXPECT_FALSE(stopsAt(specialist, *approach())) << "Level 1 neutrals count as Core";
	EXPECT_EQ(newHorizonsHeroes::specialtyRank(gameState()->getCreatureCategoryRules(), creature("core:azureDragon")), 2);
}

TEST_F(RampartAdventureSpecialtyTest, ElusiveMovesBesideTheGuardWithoutCombatButSteppingOnItFights)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:pikeman"));
	const auto tile = approach();
	ASSERT_TRUE(tile);
	handler->setMovePoints(specialist->id, 20000);
	ASSERT_TRUE(handler->moveHero(specialist->id, specialist->convertFromVisitablePos(*tile), EMovementMode::STANDARD,
		false, PLAYER, EPathfindingLayer::LAND));
	EXPECT_EQ(specialist->visitablePos(), *tile);
	EXPECT_EQ(gameState()->getBattle(PLAYER), nullptr) << "Moving beside the guard starts no fight";
	ASSERT_TRUE(handler->moveHero(specialist->id, specialist->convertFromVisitablePos(guard->visitablePos()),
		EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::LAND));
	EXPECT_EQ(specialist->visitablePos(), *tile) << "The guard blocks its tile";
	EXPECT_NE(gameState()->getBattle(PLAYER), nullptr) << "Stepping onto the guard starts the fight";
}

TEST_F(HistoricalRampartAdventureTest, HistoricalKyrreStopsBesideEveryGuard)
{
	ASSERT_NO_FATAL_FAILURE(guarded("core:pikeman"));
	EXPECT_TRUE(stopsAt(specialist, *approach()));
}

TEST_F(RampartAdventureSpecialtyTest, PatronessCollectsAShareOfTheTownIncomeOncePerWeek)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:jenova"));
	server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
	handler = std::make_unique<CGameHandler>(*server, gameState());
	specialist->level = 5;
	const auto income = town->dailyIncome()[EGameResID::GOLD];
	ASSERT_GT(income, 0);
	const auto before = gold();
	handler->heroVisitCastle(town, specialist);
	EXPECT_EQ(gold(), before + income * 350 / 1000) << "25% at level 1, +2.5% per further level";
	leave();
	handler->heroVisitCastle(town, specialist);
	EXPECT_EQ(gold(), before + income * 350 / 1000) << "Once per town and week";
	leave();
	handler->heroVisitCastle(town, ordinary);
	EXPECT_EQ(gold(), before + income * 350 / 1000) << "Only Jenova collects";
	for(int day = 0; day < 7; ++day)
		handler->onNewTurn();
	const auto nextWeek = gold();
	handler->heroVisitCastle(town, specialist);
	EXPECT_EQ(gold(), nextWeek + town->dailyIncome()[EGameResID::GOLD] * 350 / 1000) << "A new week, a new collection";
}

TEST_F(RampartAdventureSpecialtyTest, RevisedJenovaHasNoDailyGoldAndRevisedHeroesShowTheirSpecialty)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:jenova"));
	for(const auto & bonus : specialist->getExportedBonusList())
		EXPECT_FALSE(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL);
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Patroness");
	EXPECT_NE(specialist->getSpecialtyPresentation().description.find("25%"), std::string::npos);
}

TEST_F(HistoricalRampartAdventureTest, HistoricalJenovaKeepsDailyGoldAndCollectsNothing)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:jenova"));
	int income = 0;
	for(const auto & bonus : specialist->getExportedBonusList())
		if(bonus && bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL)
			income += bonus->val;
	EXPECT_GT(income, 0);
	EXPECT_TRUE(newHorizonsHeroes::patronessCollection(*specialist, *town).empty());
}

TEST_F(RampartAdventureSpecialtyTest, PatronessCollectionAndRowsSurviveSaveAndLoad)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:jenova"));
	server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
	handler = std::make_unique<CGameHandler>(*server, gameState());
	handler->heroVisitCastle(town, specialist);
	ASSERT_TRUE(newHorizonsHeroes::patronessCollection(*specialist, *town).empty());
	CGameState world;
	world.preInit(LIBRARY);
	world.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = world.getHero(specialist->id);
	const auto * loadedTown = world.getTown(town->id);
	ASSERT_TRUE(loaded && loadedTown);
	EXPECT_EQ(newHorizonsHeroes::patronessPermille(loaded), newHorizonsHeroes::patronessPermille(specialist));
	EXPECT_GT(newHorizonsHeroes::patronessPermille(loaded), 0) << "The captured Rampart row is saved with the world";
	EXPECT_TRUE(newHorizonsHeroes::patronessCollection(*loaded, *loadedTown).empty()) << "This week's collection stays spent";
}

TEST_F(RampartAdventureSpecialtyTest, PatronessCollectsOnAnUnguardedCapture)
{
	ASSERT_NO_FATAL_FAILURE(besideTown(PlayerColor::NEUTRAL, {}));
	const auto before = gold();
	// onHeroVisit -> onTownCaptured -> heroVisitCastle
	ASSERT_TRUE(handler->moveHero(specialist->id, specialist->convertFromVisitablePos(town->visitablePos()),
		EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::LAND));
	ASSERT_EQ(town->getOwner(), PLAYER);
	ASSERT_EQ(town->getVisitingHero(), specialist);
	ASSERT_GT(expectedCollection(), 0);
	EXPECT_EQ(gold(), before + expectedCollection()) << "The captured town is now hers";
}

TEST_F(RampartAdventureSpecialtyTest, PatronessCollectsWhenSheEntersAfterASiegeWin)
{
	ASSERT_NO_FATAL_FAILURE(besideTown(PlayerColor(1), {{creature("core:pikeman"), 1}}));
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	town->battleFinished(*handler, specialist, result);
	ASSERT_EQ(town->getOwner(), PLAYER);
	const auto before = gold();
	// The movement that started the siege finishes its visit after the victory
	// (CHeroMovementQuery::visitDestAfterVictory -> visitObjectOnTile).
	map()->moveObject(specialist->id, town->visitablePos() + specialist->getVisitableOffset());
	handler->visitObjectOnTile(map()->getTile(town->visitablePos()), specialist);
	ASSERT_EQ(town->getVisitingHero(), specialist);
	ASSERT_GT(expectedCollection(), 0);
	EXPECT_EQ(gold(), before + expectedCollection());
}

TEST_F(RampartAdventureSpecialtyTest, PatronessCollectsOnTheCaptureThatEndsAnOrdinarySiege)
{
	ASSERT_NO_FATAL_FAILURE(besideTown(PlayerColor(1), {{creature("core:pikeman"), 1}}));
	const auto before = gold();
	// No wandering guard: the move starts the siege itself.
	ASSERT_TRUE(handler->moveHero(specialist->id, specialist->convertFromVisitablePos(town->visitablePos()),
		EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::LAND));
	ASSERT_NE(gameState()->getBattle(PLAYER), nullptr) << "The garrison defends the town";
	handler->battles->cheatBattleVictory(PLAYER);
	for(const auto player : {PLAYER, PlayerColor(1)})
		if(const auto dialog = handler->queries->topQuery(player); dialog && dialog->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(handler->queryReply(dialog->queryID, 0, player));
	ASSERT_EQ(gameState()->getBattle(PLAYER), nullptr);
	ASSERT_EQ(town->getOwner(), PLAYER) << "battleFinished captured the town";
	const auto collected = gold() - before;
	ASSERT_GT(expectedCollection(), 0);
	EXPECT_EQ(collected, expectedCollection()) << "The capture is her arrival";
	map()->moveObject(specialist->id, town->visitablePos() + specialist->getVisitableOffset());
	handler->heroVisitCastle(town, specialist);
	EXPECT_EQ(gold(), before + collected) << "Entering the captured town this week collects nothing more";
}

TEST_F(RampartAdventureSpecialtyTest, PatronessLeavesOutJenovasOwnStewardGold)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:jenova"));
	server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
	handler = std::make_unique<CGameHandler>(*server, gameState());
	const auto plain = newHorizonsHeroes::patronessCollection(*specialist, *town)[EGameResID::GOLD];
	ASSERT_TRUE(handler->assignTownSteward(town->id, specialist->id, PLAYER));
	ASSERT_GT(town->getStewardGoldBeforeHandicap(), 0);
	EXPECT_EQ(newHorizonsHeroes::patronessCollection(*specialist, *town)[EGameResID::GOLD], plain)
		<< "Her own steward Gold is not shared back to her";
	ASSERT_TRUE(handler->assignTownSteward(town->id, ordinary->id, PLAYER));
	EXPECT_GT(newHorizonsHeroes::patronessCollection(*specialist, *town)[EGameResID::GOLD], plain)
		<< "Another hero's steward Gold is part of the town's income";
}
