/*
 * NewHorizonsMasterLogisticianTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace
{
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto SCOUTING_PERK = "new-horizons:logistics.scouting";
constexpr auto ROADMASTER_PERK = "new-horizons:logistics.roadmaster";
constexpr auto MASTER_LOGISTICIAN = "new-horizons:logistics.masterLogistician";
constexpr PlayerColor PLAYER(0);

int carriedMovement(int remaining)
{
	return 15 * std::max(0, remaining) / 100;
}

class NewHorizonsMasterLogisticianTest : public TinyMapGameTest
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
		if(!newHorizonsProfile)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
			return;
		}

		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PLAYER)
			settings.connectedPlayerIDs.clear();
	}

	void makeClearLand()
	{
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
			{
				auto & tile = map()->getTile({x, y, 0});
				tile.terrainType = ETerrainId::GRASS;
				tile.roadType = RoadId::NO_ROAD;
			}
	}

	static SecondarySkill logisticsSkill()
	{
		const int decoded = SecondarySkill::decode(LOGISTICS_SKILL);
		if(decoded < 0)
			throw std::runtime_error("New Horizons Logistics skill is not registered");
		return SecondarySkill(decoded);
	}

	static bool chooseOfferedPerk(CGameHandler & handler, CGHeroInstance * hero,
		std::string_view perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(hero, offer, index, seed, false);
				return true;
			}
		}
		return false;
	}

	void grantMasterLogistician(CGameHandler & handler, CGHeroInstance * hero)
	{
		const auto skill = logisticsSkill();
		handler.changeSecSkill(hero, skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		handler.levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::BASIC);
		ASSERT_TRUE(chooseOfferedPerk(handler, hero, SCOUTING_PERK, MasteryLevel::BASIC));

		handler.levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::ADVANCED);
		ASSERT_TRUE(chooseOfferedPerk(handler, hero, ROADMASTER_PERK, MasteryLevel::ADVANCED));

		handler.levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::EXPERT);
		ASSERT_TRUE(chooseOfferedPerk(handler, hero, MASTER_LOGISTICIAN, MasteryLevel::EXPERT));
		ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, MASTER_LOGISTICIAN));
	}

	bool newHorizonsProfile = true;
};
}

TEST_F(NewHorizonsMasterLogisticianTest, CarriesUnusedMovementForLandAndSeaAndPathfinderUsesTheOverLimitPool)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NewHorizonsMasterLogistician")
		.playerActive(PLAYER).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{pikeman, 1}})
		.hero({10, 5, 0}, HeroTypeID(1), PLAYER).heroGarrison({{pikeman, 1}})
		.hero({15, 5, 0}, HeroTypeID(2), PLAYER).heroGarrison({{pikeman, 1}})
		.hero({21, 5, 0}, HeroTypeID(3), PLAYER).heroGarrison({{pikeman, 1}});
	startWithMap(std::move(builder));
	makeClearLand();
	revealMap(PLAYER);

	auto * land100 = findHeroAt({5, 5, 0});
	auto * pathfinder = findHeroAt({10, 5, 0});
	auto * noPerk = findHeroAt({15, 5, 0});
	auto * sailor = findHeroAt({21, 5, 0});
	ASSERT_NE(land100, nullptr);
	ASSERT_NE(pathfinder, nullptr);
	ASSERT_NE(noPerk, nullptr);
	ASSERT_NE(sailor, nullptr);
	EXPECT_FALSE(gameState()->getPlayerState(PLAYER)->isHuman());
	ASSERT_TRUE(land100->usesNewHorizonsMovement());
	ASSERT_TRUE(pathfinder->usesNewHorizonsMovement());
	ASSERT_TRUE(noPerk->usesNewHorizonsMovement());
	ASSERT_TRUE(sailor->usesNewHorizonsMovement());

	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	grantMasterLogistician(handler, land100);
	grantMasterLogistician(handler, pathfinder);
	grantMasterLogistician(handler, sailor);
	const auto skill = logisticsSkill();
	handler.changeSecSkill(noPerk, skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(noPerk->hasActivePerk(LOGISTICS_SKILL, MASTER_LOGISTICIAN));

	const int landLimit = land100->movementPointsLimit();
	const int pathLimit = pathfinder->movementPointsLimit();
	const int noPerkLimit = noPerk->movementPointsLimit();
	ASSERT_EQ(landLimit, 260);
	ASSERT_EQ(pathLimit, landLimit);
	ASSERT_EQ(noPerkLimit, landLimit);

	const int3 boatPosition = sailor->visitablePos() + int3(1, 0, 0);
	map()->getTile(boatPosition).terrainType = ETerrainId::WATER;
	handler.createBoat(boatPosition, BoatId::CASTLE, PLAYER);
	ASSERT_TRUE(handler.moveHero(sailor->id, sailor->convertFromVisitablePos(boatPosition),
		EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::SAIL));
	ASSERT_TRUE(sailor->inBoat());
	const int waterLimit = sailor->movementPointsLimit();
	ASSERT_GT(waterLimit, 0);

	// The initial day grants only the ordinary refill; the carry begins after a
	// completed day and is based on the previous day's remaining Movement.
	handler.setMovePoints(land100->id, 100);
	handler.setMovePoints(pathfinder->id, 100);
	handler.setMovePoints(noPerk->id, 100);
	handler.setMovePoints(sailor->id, 100);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);
	EXPECT_EQ(land100->movementPointsRemaining(), landLimit);
	EXPECT_EQ(pathfinder->movementPointsRemaining(), pathLimit);
	EXPECT_EQ(noPerk->movementPointsRemaining(), noPerkLimit);
	EXPECT_EQ(sailor->movementPointsRemaining(), waterLimit);

	// 100 unused points produce exactly 15 carry. An Expert Logistics hero
	// without the selected Expert perk receives only its ordinary refill.
	handler.setMovePoints(land100->id, 100);
	handler.setMovePoints(pathfinder->id, pathLimit);
	handler.setMovePoints(noPerk->id, 100);
	handler.setMovePoints(sailor->id, 100);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 2);
	EXPECT_EQ(land100->movementPointsRemaining(), landLimit + 15);
	EXPECT_EQ(pathfinder->movementPointsRemaining(), pathLimit + carriedMovement(pathLimit));
	EXPECT_EQ(noPerk->movementPointsRemaining(), noPerkLimit);
	EXPECT_EQ(sailor->movementPointsRemaining(), waterLimit + 15);
	EXPECT_TRUE(sailor->inBoat());
	EXPECT_EQ(land100->movementPointsLimit(), landLimit);
	EXPECT_EQ(sailor->movementPointsLimit(), waterLimit);

	// The second carry is recomputed from current unused Movement rather than
	// adding a second percentage bonus to the previous day's carry.
	handler.setMovePoints(land100->id, 99);
	handler.setMovePoints(noPerk->id, 99);
	handler.setMovePoints(sailor->id, 99);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 3);
	EXPECT_EQ(land100->movementPointsRemaining(), landLimit + 14);
	EXPECT_EQ(noPerk->movementPointsRemaining(), noPerkLimit);
	EXPECT_EQ(sailor->movementPointsRemaining(), waterLimit + 14);
	const int expectedPathfinderMovement = pathLimit + carriedMovement(pathLimit + carriedMovement(pathLimit));
	EXPECT_EQ(pathfinder->movementPointsRemaining(), expectedPathfinderMovement);
	EXPECT_EQ(pathfinder->movementPointsRemaining(), pathLimit + 44);
	EXPECT_TRUE(sailor->inBoat());

	// The map pathfinder can spend the carry on a same-day route while its
	// ordinary daily movement limit remains unchanged.
	// On this clear New Horizons route each orthogonal step costs 10 points;
	// 27 steps therefore exceed the ordinary 260 pool while fitting in the
	// 304-point carried pool at this event boundary.
	const int3 overLimitDestination = pathfinder->visitablePos() + int3(0, 27, 0);
	handler.setMovePoints(pathfinder->id, pathLimit);
	PathfinderCache ordinaryPaths(gameState().get(), PathfinderOptions(*gameState()));
	const auto ordinaryInfo = ordinaryPaths.getPathsInfo(pathfinder);
	const auto ordinaryPath = ordinaryInfo->getNode(overLimitDestination, EPathfindingLayer::LAND);
	ASSERT_NE(ordinaryPath, nullptr);
	EXPECT_GT(ordinaryPath->turns, 0);
	handler.setMovePoints(pathfinder->id, expectedPathfinderMovement);
	PathfinderCache carriedPaths(gameState().get(), PathfinderOptions(*gameState()));
	const auto carriedInfo = carriedPaths.getPathsInfo(pathfinder);
	const auto carriedPath = carriedInfo->getNode(overLimitDestination, EPathfindingLayer::LAND);
	ASSERT_NE(carriedPath, nullptr);
	EXPECT_EQ(carriedPath->turns, 0);
	const auto currentTurn = pathfinder->getTurnInfo(0);
	const auto nextTurn = pathfinder->getTurnInfo(1);
	ASSERT_NE(currentTurn, nullptr);
	ASSERT_NE(nextTurn, nullptr);
	EXPECT_EQ(currentTurn->getMovePointsLimitLand(), pathLimit);
	EXPECT_EQ(nextTurn->getMovePointsLimitLand(), pathLimit);
	const auto seaTurn = sailor->getTurnInfo(0);
	ASSERT_NE(seaTurn, nullptr);
	EXPECT_EQ(seaTurn->getMovePointsLimitWater(), waterLimit);

	// Zero unused movement cannot create carry on the next completed day.
	handler.setMovePoints(land100->id, 0);
	handler.setMovePoints(pathfinder->id, 0);
	handler.setMovePoints(noPerk->id, 0);
	handler.setMovePoints(sailor->id, 0);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 4);
	EXPECT_EQ(land100->movementPointsRemaining(), landLimit);
	EXPECT_EQ(pathfinder->movementPointsRemaining(), pathLimit);
	EXPECT_EQ(noPerk->movementPointsRemaining(), noPerkLimit);
	EXPECT_EQ(sailor->movementPointsRemaining(), waterLimit);
}

TEST_F(NewHorizonsMasterLogisticianTest, CastleStablesRefillPreservesCarryAndSaveRestoresMovementAndPerk)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NewHorizonsMasterLogisticianStables")
		.playerActive(PLAYER).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{pikeman, 1}})
		.town({18, 18, 0}, FactionID::CASTLE, PLAYER).townGarrison({});
	startWithMap(std::move(builder));
	makeClearLand();

	auto * hero = findHeroAt({5, 5, 0});
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	ASSERT_FALSE(gameState()->getPlayerState(PLAYER)->isHuman());
	ASSERT_TRUE(hero->usesNewHorizonsMovement());

	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	grantMasterLogistician(handler, hero);
	ASSERT_EQ(hero->movementPointsLimit(), 260);
	ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::SPECIAL_2, true));
	town->setVisitingHero(hero);

	// The first day has no carry, but the day-start Stables grant raises the
	// effective land limit by its canonical 20 percentage points.
	const int normalLimit = hero->movementPointsLimit();
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);
	ASSERT_EQ(normalLimit, 260);
	ASSERT_EQ(hero->movementPointsLimit(), 300);
	EXPECT_EQ(hero->movementPointsRemaining(), 300);

	// A resident's fresh Stables refill keeps, rather than clamps away, carry
	// produced from the prior normal maximum. The next-day forecast still uses
	// the ordinary pool once today's one-day bonus expires.
	handler.setMovePoints(hero->id, 100);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 2);
	EXPECT_EQ(hero->movementPointsLimit(), 300);
	EXPECT_EQ(hero->movementPointsRemaining(), 315);
	const auto currentTurn = hero->getTurnInfo(0);
	const auto nextTurn = hero->getTurnInfo(1);
	ASSERT_NE(currentTurn, nullptr);
	ASSERT_NE(nextTurn, nullptr);
	EXPECT_EQ(currentTurn->getMovePointsLimitLand(), 300);
	EXPECT_EQ(nextTurn->getMovePointsLimitLand(), 260);

	const auto heroId = hero->id;
	const auto savedMovement = hero->movementPointsRemaining();
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredHero = restored.getHero(heroId);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->movementPointsRemaining(), savedMovement);
	EXPECT_EQ(restoredHero->movementPointsLimit(), 300);
	EXPECT_TRUE(restoredHero->hasActivePerk(LOGISTICS_SKILL, MASTER_LOGISTICIAN));
	EXPECT_EQ(restored.getTown(town->id)->getVisitingHero(), restoredHero);

	// Repeated resident days refresh one daily Stables bonus; they do not add
	// the previous carry a second time when only 99 points remain.
	handler.setMovePoints(hero->id, 99);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 3);
	EXPECT_EQ(hero->movementPointsLimit(), 300);
	EXPECT_EQ(hero->movementPointsRemaining(), 314);
}

TEST_F(NewHorizonsMasterLogisticianTest, LegacyHeroMovementRefillDoesNotGainNewHorizonsCarry)
{
	newHorizonsProfile = false;
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("LegacyMasterLogisticianGuard")
		.playerActive(PLAYER)
		.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{pikeman, 1}});
	startWithMap(std::move(builder));

	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_FALSE(hero->usesNewHorizonsMovement());
	const int ordinaryLimit = hero->movementPointsLimit();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.setMovePoints(hero->id, 100);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);
	EXPECT_EQ(hero->movementPointsRemaining(), ordinaryLimit);
	handler.setMovePoints(hero->id, 100);
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 2);
	EXPECT_EQ(hero->movementPointsRemaining(), ordinaryLimit);
}
