/*
 * NewHorizonsLogisticsMovementPerksTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto SCOUTING_PERK = "new-horizons:logistics.scouting";
constexpr auto ROADMASTER_PERK = "new-horizons:logistics.roadmaster";
constexpr auto WAYFARER_PERK = "new-horizons:logistics.wayfarer";

void setPerkStatus(JsonNode & rules, const std::string & perkId, const std::string & status)
{
	for(auto & skillEntry : rules["skills"].Struct())
		for(auto & perk : skillEntry.second["perks"].Vector())
			if(perk["id"].String() == perkId)
			{
				perk["effect"]["status"].String() = status;
				return;
			}
	throw std::runtime_error("Missing New Horizons perk in Logistics movement fixture: " + perkId);
}

class NewHorizonsLogisticsMovementPerksTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));

		legacyPerkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto activePerkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		setPerkStatus(activePerkRules, ROADMASTER_PERK, "active");
		setPerkStatus(activePerkRules, WAYFARER_PERK, "active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(activePerkRules));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsLogisticsMovementPerks")
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(16), PlayerColor(0)) // Mephala; Rampart's native terrain is grass.
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		revealMap(PlayerColor(0));

		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(hero->usesNewHorizonsMovement());

		server = std::make_unique<GameHandlerTestServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
				setTile({x, y, 0}, ETerrainId::GRASS, RoadId::NO_ROAD);
	}

	SecondarySkill logisticsSkill() const
	{
		const int decoded = SecondarySkill::decode(LOGISTICS_SKILL);
		if(decoded < 0)
			throw std::runtime_error("New Horizons Logistics skill is not registered");
		return SecondarySkill(decoded);
	}

	bool chooseOfferedPerk(const std::string & perkId)
	{
		const auto rankLookup = [this](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offer.begin(), offer.end(), [&perkId](const auto & entry)
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

	void setTile(const int3 & position, const TerrainId terrain, const RoadId road)
	{
		auto & tile = map()->getTile(position);
		tile.terrainType = terrain;
		tile.roadType = road;
	}

	std::optional<int> forecastCost(const int3 & destination)
	{
		const int movementBefore = hero->movementPointsRemaining();
		PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
		const auto paths = cache.getPathsInfo(hero);
		const auto * node = paths->getNode(destination, EPathfindingLayer::LAND);
		if(!node || !node->reachable() || node->turns != 0)
			return std::nullopt;
		return movementBefore - node->moveRemains;
	}

	void expectAcceptedMove(const int3 & destination, const int expectedCost)
	{
		const int movementBefore = hero->movementPointsRemaining();
		const auto forecast = forecastCost(destination);
		ASSERT_TRUE(forecast.has_value());
		ASSERT_EQ(*forecast, expectedCost);
		ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(destination),
			EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
		EXPECT_EQ(hero->movementPointsRemaining(), movementBefore - *forecast);
	}

	void advanceLogistics()
	{
		handler->levelUpHero(hero, logisticsSkill(), false);
	}

	void installLegacyPlannedSnapshot()
	{
		// Simulate a hero loaded with the older saved rules snapshot. The perk
		// identities were acquired above through validated offers; only their
		// captured activation status is restored here.
		setPerkStatus(legacyPerkRules, ROADMASTER_PERK, "planned");
		setPerkStatus(legacyPerkRules, WAYFARER_PERK, "planned");
		auto savedState = hero->getPerkState().toJson();
		savedState["rules"] = legacyPerkRules;
		auto restoredState = newHorizonsHeroes::PerkState::fromJson(savedState);
		const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()) = std::move(restoredState);
	}

	JsonNode legacyPerkRules;
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, RoadmasterForecastMatchesOrthogonalDiagonalAndOffroadMovement)
{
	startGame();
	const auto source = hero->visitablePos();
	const auto orthogonal = source + int3(1, 0, 0);
	const auto diagonal = orthogonal + int3(1, 1, 0);
	const auto offroad = diagonal + int3(1, 0, 0);
	setTile(source, ETerrainId::GRASS, RoadId::DIRT_ROAD);
	setTile(orthogonal, ETerrainId::GRASS, RoadId::DIRT_ROAD);
	setTile(diagonal, ETerrainId::GRASS, RoadId::DIRT_ROAD);
	setTile(offroad, ETerrainId::GRASS, RoadId::NO_ROAD);

	advanceLogistics(); // Basic Logistics.
	const auto basicOffer = hero->getPerkState().prepareOffer([this](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, 7);
	EXPECT_TRUE(std::none_of(basicOffer.begin(), basicOffer.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == ROADMASTER_PERK || candidate.selection.perkId == WAYFARER_PERK;
	}));
	EXPECT_THROW(advanceLogistics(), std::runtime_error);
	ASSERT_TRUE(chooseOfferedPerk(SCOUTING_PERK));
	advanceLogistics(); // Advanced Logistics.
	EXPECT_THROW(advanceLogistics(), std::runtime_error);
	ASSERT_TRUE(chooseOfferedPerk(ROADMASTER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, ROADMASTER_PERK));
	const auto advancedOffer = hero->getPerkState().prepareOffer([this](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, 13);
	EXPECT_TRUE(std::none_of(advancedOffer.begin(), advancedOffer.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == WAYFARER_PERK;
	}));

	// Rampart's native grass road costs 7 before Roadmaster and 6 after it.
	EXPECT_EQ(forecastCost(orthogonal), 6);
	expectAcceptedMove(orthogonal, 6);
	expectAcceptedMove(diagonal, 8);
	expectAcceptedMove(offroad, 10);
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, WayfarerCapsPassableTerrainAndPreservesSavedPlannedAndBlockedGuards)
{
	startGame();
	const auto source = hero->visitablePos();
	const auto destination = source + int3(1, 0, 0);
	advanceLogistics(); // Basic Logistics.
	ASSERT_TRUE(chooseOfferedPerk(SCOUTING_PERK));
	advanceLogistics(); // Advanced Logistics.
	ASSERT_TRUE(chooseOfferedPerk(ROADMASTER_PERK));
	advanceLogistics(); // Expert Logistics.
	ASSERT_TRUE(chooseOfferedPerk(WAYFARER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, WAYFARER_PERK));

	setTile(source, ETerrainId::GRASS, RoadId::NO_ROAD);
	setTile(destination, ETerrainId::GRASS, RoadId::NO_ROAD);
	EXPECT_EQ(forecastCost(destination), 10); // Rampart native terrain.
	setTile(source, ETerrainId::DIRT, RoadId::NO_ROAD);
	setTile(destination, ETerrainId::DIRT, RoadId::NO_ROAD);
	EXPECT_EQ(forecastCost(destination), 13); // Wayfarer's 125% cap applies to 140% terrain.
	setTile(source, ETerrainId::SAND, RoadId::NO_ROAD);
	setTile(destination, ETerrainId::SAND, RoadId::NO_ROAD);
	expectAcceptedMove(destination, 13); // Wayfarer's cap is reflected in the real move.

	const auto roadDestination = destination + int3(1, 0, 0);
	setTile(destination, ETerrainId::SAND, RoadId::DIRT_ROAD);
	setTile(roadDestination, ETerrainId::SAND, RoadId::DIRT_ROAD);
	expectAcceptedMove(roadDestination, 7); // Terrain cap, then Roadmaster, one final ceiling.

	installLegacyPlannedSnapshot();
	EXPECT_FALSE(hero->hasActivePerk(LOGISTICS_SKILL, ROADMASTER_PERK));
	EXPECT_FALSE(hero->hasActivePerk(LOGISTICS_SKILL, WAYFARER_PERK));
	const auto legacyRoadDestination = roadDestination + int3(1, 0, 0);
	setTile(roadDestination, ETerrainId::SAND, RoadId::DIRT_ROAD);
	setTile(legacyRoadDestination, ETerrainId::SAND, RoadId::DIRT_ROAD);
	expectAcceptedMove(legacyRoadDestination, 13);

	const auto rock = legacyRoadDestination + int3(1, 0, 0);
	setTile(rock, ETerrainId::ROCK, RoadId::NO_ROAD);
	const auto positionBeforeBlockedMove = hero->visitablePos();
	const int movementBeforeBlockedMove = hero->movementPointsRemaining();
	EXPECT_FALSE(handler->moveHero(hero->id, hero->convertFromVisitablePos(rock),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), positionBeforeBlockedMove);
	EXPECT_EQ(hero->movementPointsRemaining(), movementBeforeBlockedMove);
}
