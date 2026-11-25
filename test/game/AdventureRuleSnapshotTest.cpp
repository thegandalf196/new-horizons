/*
 * AdventureRuleSnapshotTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 *
 * Rule facts read once per pathfinder helper or captured rules owner must give
 * the same answers as the per-call reads they replace.
 */
#include "StdInc.h"

#include "GameStateTest.h"
#include "../mock/GameHandlerTestServer.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/NewHorizonsWarfare.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/NewHorizonsProtectedMobility.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../server/CGameHandler.h"

namespace
{
class AdventureRuleSnapshotTest : public GameStateTest
{
protected:
	CGHeroInstance * startWithHero()
	{
		startTestGame();
		const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
		return heroes.empty() ? nullptr : heroes.front();
	}

	/// Every map tile: the snapshot answer equals the scanning dig-status read.
	void expectHoleAnswersMatchDigStatus(const CGHeroInstance * hero)
	{
		const CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
		const auto size = gameState->getMapSize();
		for(int z = 0; z < size.z; ++z)
			for(int y = 0; y < size.y; ++y)
				for(int x = 0; x < size.x; ++x)
				{
					const int3 tile(x, y, z);
					ASSERT_EQ(helper.isTileBlockedByHole(tile),
						gameState->getTileDigStatus(tile, false) == EDiggingStatus::TILE_OCCUPIED) << tile.toString();
				}
	}

	/// An empty, walkable land tile next to the hero, where only a hole can block digging.
	std::optional<int3> emptyLandNear(const CGHeroInstance * hero)
	{
		for(const auto & offset : {int3(2, 0, 0), int3(-2, 0, 0), int3(0, 2, 0), int3(0, -2, 0), int3(2, 2, 0)})
		{
			const int3 tile = hero->visitablePos() + offset;
			if(!gameState->isInTheMap(tile))
				continue;
			auto & terrain = gameState->getMap().getTile(tile);
			if(!terrain.blockingObjects.empty() || !terrain.visitableObjects.empty())
				continue;
			terrain.terrainType = ETerrainId::GRASS;
			if(terrain.getDiggingStatus() == EDiggingStatus::CAN_DIG)
				return tile;
		}
		return std::nullopt;
	}
};
}

TEST_F(AdventureRuleSnapshotTest, holeCheckMatchesDigStatusBeforeAndAfterHolesAreDug)
{
	auto * hero = startWithHero();
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(gameState->getSettings().getBoolean(EGameSettings::PATHFINDER_BLOCK_DISEMBARK_ON_HOLE));
	const auto target = emptyLandNear(hero);
	ASSERT_TRUE(target.has_value());
	ASSERT_NO_FATAL_FAILURE(expectHoleAnswersMatchDigStatus(hero));

	const CPathfinderHelper before(*gameState, hero, PathfinderOptions(*gameState));
	EXPECT_FALSE(before.isTileBlockedByHole(*target));

	GameHandlerTestServer server(gameState);
	CGameHandler gameHandler(server, gameState);
	gameHandler.createHole(*target, hero->getOwner());
	// Only the new hole object blocks this tile; its terrain alone could still be dug.
	ASSERT_EQ(gameState->getTileDigStatus(*target, false), EDiggingStatus::TILE_OCCUPIED);
	ASSERT_EQ(gameState->getMap().getTile(*target).getDiggingStatus(), EDiggingStatus::CAN_DIG);

	const CPathfinderHelper after(*gameState, hero, PathfinderOptions(*gameState));
	EXPECT_TRUE(after.isTileBlockedByHole(*target));
	ASSERT_NO_FATAL_FAILURE(expectHoleAnswersMatchDigStatus(hero));

	// The hero's own dig goes through the same object creation.
	if(hero->diggingStatus() == EDiggingStatus::CAN_DIG)
	{
		ASSERT_TRUE(gameHandler.dig(hero));
		ASSERT_NO_FATAL_FAILURE(expectHoleAnswersMatchDigStatus(hero));
	}
}

TEST_F(AdventureRuleSnapshotTest, capturedMobilityFlagsAndWarfarePolicyMatchTheirRules)
{
	const auto * hero = startWithHero();
	ASSERT_NE(hero, nullptr);
	const CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	EXPECT_EQ(helper.protectedMobilityEnabled(), newHorizonsProtectedMobility::enabled(hero->getMagicRules()));
	EXPECT_EQ(helper.tracksProtectedFlight(), newHorizonsProtectedMobility::tracksFlight(hero->getMagicRules()));

	const auto & rules = hero->getPerkState().getRules();
	const auto policy = newHorizonsWarfare::policy(hero);
	ASSERT_EQ(policy.has_value(), newHorizonsWarfare::usesRules(rules));
	if(policy)
		EXPECT_EQ(*policy, newHorizonsWarfare::readPolicy(rules));
}

using WarfarePolicyCaptureTest = vcmiTestData::OriginalDataTest;

TEST_F(WarfarePolicyCaptureTest, eachCaptureKeepsItsOwnParsedPolicy)
{
	const JsonNode raw(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsWarfare::usesRules(raw));
	const auto owner = nativeRules::CapturedPerkRules::compile(raw);
	const auto * cached = owner->warfarePolicy();
	ASSERT_NE(cached, nullptr);
	EXPECT_EQ(*cached, newHorizonsWarfare::readPolicy(owner->document()));
	EXPECT_EQ(owner->warfarePolicy(), cached);

	// A new game or a load captures a new owner, which reads its own rules.
	auto edited = raw;
	auto & expert = edited["skills"][newHorizonsWarfare::SKILL]["ranks"]["expert"]["effect"]["parameters"];
	expert["orderEfficiencyPercent"].Integer() = cached->rankEfficiencyPercent[3] + 7;
	const auto reloaded = nativeRules::CapturedPerkRules::compile(edited);
	ASSERT_NE(reloaded->warfarePolicy(), nullptr);
	EXPECT_EQ(*reloaded->warfarePolicy(), newHorizonsWarfare::readPolicy(edited));
	EXPECT_EQ(reloaded->warfarePolicy()->rankEfficiencyPercent[3], cached->rankEfficiencyPercent[3] + 7);
	EXPECT_EQ(*owner->warfarePolicy(), newHorizonsWarfare::readPolicy(raw));

	EXPECT_EQ(nativeRules::CapturedPerkRules::compile(JsonNode())->warfarePolicy(), nullptr);
}

TEST(AdventureAiLogDefaults, aiFileLoggingDefaultsToDebugWhileGlobalStaysTrace)
{
	const JsonNode schema(JsonPath::builtin("config/schemas/settings.json"));
	std::map<std::string, std::string> levels;
	for(const auto & logger : schema["properties"]["logging"]["properties"]["loggers"]["default"].Vector())
		levels[logger["domain"].String()] = logger["level"].String();
	EXPECT_EQ(levels["global"], "trace");
	EXPECT_EQ(levels["rng"], "info");
	EXPECT_EQ(levels["ai"], "debug");

	// At that level AI trace lines are dropped before formatting or file writes.
	auto * ai = CLogger::getLogger(CLoggerDomain("ai"));
	const auto previous = ai->getLevel();
	ai->setLevel(ELogLevel::DEBUG);
	EXPECT_FALSE(ai->isTraceEnabled());
	EXPECT_TRUE(ai->isDebugEnabled());
	ai->setLevel(previous);
}
