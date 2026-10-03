/*
 * NewHorizonsLogisticsMovementAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/pathfinder/CPathfinder.h"
#include "lib/pathfinder/PathfinderOptions.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto PATHFINDING_PERK = "new-horizons:logistics.pathfinding";
constexpr auto SCOUTING_PERK = "new-horizons:logistics.scouting";
constexpr auto ROADMASTER_PERK = "new-horizons:logistics.roadmaster";
constexpr auto WAYFARER_PERK = "new-horizons:logistics.wayfarer";
constexpr auto MOUNTAINEER_PERK = "new-horizons:logistics.mountaineer";

void activateMapPerk(JsonNode & rules, std::string_view perkId)
{
	for(auto & [skillId, skill] : rules["skills"].Struct())
		for(auto & perk : skill["perks"].Vector())
			if(perk["id"].String() == perkId)
			{
				perk["effect"]["status"].String() = "active";
				return;
			}
	throw std::runtime_error("Missing New Horizons movement perk in AI fixture: " + std::string(perkId));
}

std::string mapPerkStatus(JsonNode & rules, std::string_view perkId)
{
	for(auto & [skillId, skill] : rules["skills"].Struct())
		for(auto & perk : skill["perks"].Vector())
			if(perk["id"].String() == perkId)
				return perk["effect"]["status"].String();
	throw std::runtime_error("Missing New Horizons movement perk in AI fixture: " + std::string(perkId));
}

class RecordingPerkServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;

	std::vector<std::string> acceptedPerks;

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * chosen = dynamic_cast<const HeroPerkChosen *>(&pack))
			acceptedPerks.push_back(chosen->selection.perkId);
		GameHandlerTestServer::applyPack(pack);
	}
};

class NewHorizonsLogisticsMovementAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));

		// Preserve the existing fixture activation for these Logistics tests.
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		activateMapPerk(perkRules, ROADMASTER_PERK);
		activateMapPerk(perkRules, WAYFARER_PERK);
		const auto mountaineerRegistryStatus = mapPerkStatus(perkRules, MOUNTAINEER_PERK);
		RecordProperty("mountaineer_registry_status", mountaineerRegistryStatus);
		if(mountaineerRegistryStatus == "planned")
		{
			activateMapPerk(perkRules, MOUNTAINEER_PERK);
			RecordProperty("mountaineer_fixture_override", "planned_to_active");
		}
		else if(mountaineerRegistryStatus == "active")
			RecordProperty("mountaineer_fixture_override", "none");
		else
			throw std::runtime_error("Unexpected Mountaineer status in AI fixture: " + mountaineerRegistryStatus);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsLogisticsMovementAI")
			.playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(16), PLAYER) // Mephala; Rampart's native terrain is grass.
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		revealMap(PLAYER);

		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(hero->usesNewHorizonsMovement());

		server = std::make_unique<RecordingPerkServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
			{
				auto & tile = map()->getTile({x, y, 0});
				tile.terrainType = ETerrainId::GRASS;
				tile.roadType = RoadId::NO_ROAD;
			}
	}

	SecondarySkill logisticsSkill() const
	{
		const int decoded = SecondarySkill::decode(LOGISTICS_SKILL);
		if(decoded < 0)
			throw std::runtime_error("New Horizons Logistics skill is not registered");
		return SecondarySkill(decoded);
	}

	void advanceLogistics()
	{
		const int currentRank = hero->getPerkSkillRank(LOGISTICS_SKILL);
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(LOGISTICS_SKILL, currentRank));
		handler->levelUpHero(hero, logisticsSkill(), false);
	}

	bool chooseOfferedPerk(std::string_view perkId)
	{
		const auto rankLookup = [this](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offer.begin(), offer.end(), [perkId](const auto & entry)
			{
				return entry.selection.perkId == perkId;
			});
			if(candidate == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), candidate));
			handler->levelUpHero(hero, offer, choice, seed, false);
			return true;
		}
		return false;
	}

	int landMovementLimit(const std::shared_ptr<CCallback> & callback) const
	{
		const PathfinderOptions options(*callback);
		CPathfinderHelper helper(*callback, hero, options);
		return helper.getMaxMovePoints(EPathfindingLayer::LAND);
	}

	CGHeroInstance * hero = nullptr;
	std::unique_ptr<RecordingPerkServer> server;
	std::unique_ptr<CGameHandler> handler;
};
} // namespace

TEST_F(NewHorizonsLogisticsMovementAITest, AcceptedRoadmasterAndWayfarerRefreshProjectedRouteCosts)
{
	startGame();
	const auto source = hero->visitablePos();
	const auto roadDestination = source + int3(1, 0, 0);
	const auto terrainDestination = source + int3(0, 1, 0);
	// The shared movement helper prices the source terrain. Use sand for both
	// adjacent routes so the road discount and terrain cap are independently
	// visible without moving the hero between perk selections.
	map()->getTile(source).terrainType = ETerrainId::SAND;
	map()->getTile(roadDestination).terrainType = ETerrainId::SAND;
	map()->getTile(source).roadType = RoadId::DIRT_ROAD;
	map()->getTile(roadDestination).roadType = RoadId::DIRT_ROAD;
	map()->getTile(terrainDestination).terrainType = ETerrainId::SAND;

	advanceLogistics(); // Basic Logistics.
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::BASIC);
	ASSERT_TRUE(chooseOfferedPerk(SCOUTING_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, SCOUTING_PERK));
	EXPECT_FALSE(hero->hasActivePerk(LOGISTICS_SKILL, "new-horizons:logistics.pathfinding"));
	ASSERT_FALSE(hero->hasNewHorizonsTerrainAffinity(ETerrainId::SAND));

	auto callback = makeCallback(PLAYER);
	auto gateway = makeGateway(callback);
	NK2AI::Goals::TGoalVec priorityTasks;
	const auto projectedCost = [&](const int3 & destination) -> std::optional<float>
	{
		const auto paths = gateway->nullkiller->pathfinder->getPathInfo(destination);
		if(paths.empty())
			return std::nullopt;
		return paths.front().movementCost();
	};

	const auto initialPosition = hero->visitablePos();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const auto baselineRoad = projectedCost(roadDestination);
	const auto baselineTerrain = projectedCost(terrainDestination);
	ASSERT_TRUE(baselineRoad.has_value());
	ASSERT_TRUE(baselineTerrain.has_value());
	const int basicMovementLimit = landMovementLimit(callback);
	ASSERT_GT(basicMovementLimit, 0);
	EXPECT_NEAR(*baselineRoad * basicMovementLimit, 13.0f, 0.01f);
	EXPECT_NEAR(*baselineTerrain * basicMovementLimit, 18.0f, 0.01f);

	advanceLogistics(); // Advanced Logistics, legally unlocked by Basic Scouting.
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::ADVANCED);
	ASSERT_TRUE(chooseOfferedPerk(ROADMASTER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, ROADMASTER_PERK));
	ASSERT_EQ(server->acceptedPerks.back(), ROADMASTER_PERK);
	gateway->invalidatePaths(); // The HeroPerkChosen client visitor uses this hook.
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const auto roadmasterRoad = projectedCost(roadDestination);
	const auto roadmasterTerrain = projectedCost(terrainDestination);
	ASSERT_TRUE(roadmasterRoad.has_value());
	ASSERT_TRUE(roadmasterTerrain.has_value());
	const int advancedMovementLimit = landMovementLimit(callback);
	ASSERT_GT(advancedMovementLimit, 0);
	EXPECT_NEAR(*roadmasterRoad * advancedMovementLimit, 10.0f, 0.01f);
	EXPECT_NEAR(*roadmasterTerrain * advancedMovementLimit, 18.0f, 0.01f);
	EXPECT_EQ(hero->visitablePos(), initialPosition);

	advanceLogistics(); // Expert Logistics, legally unlocked by Roadmaster.
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::EXPERT);
	ASSERT_TRUE(chooseOfferedPerk(WAYFARER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, WAYFARER_PERK));
	ASSERT_EQ(server->acceptedPerks.back(), WAYFARER_PERK);
	gateway->invalidatePaths(); // Refresh the second accepted perk's cached route as well.
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 3));
	const auto wayfarerRoad = projectedCost(roadDestination);
	const auto wayfarerTerrain = projectedCost(terrainDestination);
	ASSERT_TRUE(wayfarerRoad.has_value());
	ASSERT_TRUE(wayfarerTerrain.has_value());
	const int expertMovementLimit = landMovementLimit(callback);
	ASSERT_GT(expertMovementLimit, 0);
	EXPECT_NEAR(*wayfarerRoad * expertMovementLimit, 7.0f, 0.01f);
	EXPECT_NEAR(*wayfarerTerrain * expertMovementLimit, 13.0f, 0.01f);
	EXPECT_EQ(hero->visitablePos(), initialPosition);
	ASSERT_EQ(server->acceptedPerks.size(), 3u);
	EXPECT_EQ(server->acceptedPerks[0], SCOUTING_PERK);
	EXPECT_EQ(server->acceptedPerks[1], ROADMASTER_PERK);
	EXPECT_EQ(server->acceptedPerks[2], WAYFARER_PERK);
}

TEST_F(NewHorizonsLogisticsMovementAITest, AcceptedMountaineerRefreshesRoughAndSubterraneanRouteCosts)
{
	startGame();
	const auto source = hero->visitablePos();
	const auto destination = source + int3(1, 0, 0);
	auto & sourceTile = map()->getTile(source);
	auto & destinationTile = map()->getTile(destination);
	sourceTile.terrainType = ETerrainId::ROUGH;
	destinationTile.terrainType = ETerrainId::ROUGH;

	advanceLogistics(); // Basic Logistics.
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::BASIC);
	ASSERT_TRUE(chooseOfferedPerk(PATHFINDING_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, PATHFINDING_PERK));

	auto callback = makeCallback(PLAYER);
	auto gateway = makeGateway(callback);
	NK2AI::Goals::TGoalVec priorityTasks;
	const auto projectedCost = [&](const int3 & target) -> std::optional<float>
	{
		const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
		if(paths.empty())
			return std::nullopt;
		return paths.front().movementCost();
	};

	const auto initialPosition = hero->visitablePos();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const int basicMovementLimit = landMovementLimit(callback);
	ASSERT_GT(basicMovementLimit, 0);
	auto withoutMountaineer = projectedCost(destination);
	ASSERT_TRUE(withoutMountaineer.has_value());
	EXPECT_NEAR(*withoutMountaineer * basicMovementLimit, 12.0f, 0.01f);

	advanceLogistics(); // Advanced Logistics, legally unlocked by Basic Pathfinding.
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::ADVANCED);
	ASSERT_TRUE(chooseOfferedPerk(MOUNTAINEER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, MOUNTAINEER_PERK));
	ASSERT_EQ(server->acceptedPerks.back(), MOUNTAINEER_PERK);
	gateway->invalidatePaths();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const int advancedMovementLimit = landMovementLimit(callback);
	ASSERT_GT(advancedMovementLimit, 0);
	auto withMountaineer = projectedCost(destination);
	ASSERT_TRUE(withMountaineer.has_value());
	EXPECT_NEAR(*withMountaineer * advancedMovementLimit, 10.0f, 0.01f);

	sourceTile.terrainType = ETerrainId::SUBTERRANEAN;
	destinationTile.terrainType = ETerrainId::SUBTERRANEAN;
	gateway->invalidatePaths();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 3));
	auto subterraneanCost = projectedCost(destination);
	ASSERT_TRUE(subterraneanCost.has_value());
	EXPECT_NEAR(*subterraneanCost * advancedMovementLimit, 10.0f, 0.01f);
	EXPECT_EQ(hero->visitablePos(), initialPosition);
	ASSERT_EQ(server->acceptedPerks.size(), 2u);
	EXPECT_EQ(server->acceptedPerks[0], PATHFINDING_PERK);
	EXPECT_EQ(server->acceptedPerks[1], MOUNTAINEER_PERK);
}
