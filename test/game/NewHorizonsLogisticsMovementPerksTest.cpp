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
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto PATHFINDING_PERK = "new-horizons:logistics.pathfinding";
constexpr auto SCOUTING_PERK = "new-horizons:logistics.scouting";
constexpr auto NAVIGATION_PERK = "new-horizons:logistics.navigation";
constexpr auto ROADMASTER_PERK = "new-horizons:logistics.roadmaster";
constexpr auto WAYFARER_PERK = "new-horizons:logistics.wayfarer";
constexpr auto MOUNTAINEER_PERK = "new-horizons:logistics.mountaineer";
constexpr auto RAPID_EMBARKATION_PERK = "new-horizons:logistics.rapidEmbarkation";

HeroTypeID heroType(const char * id)
{
	const int decoded = HeroTypeID::decode(id);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing hero in Logistics movement fixture: ") + id);
	return HeroTypeID(decoded);
}

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

std::string getPerkStatus(JsonNode & rules, const std::string & perkId)
{
	for(auto & skillEntry : rules["skills"].Struct())
		for(auto & perk : skillEntry.second["perks"].Vector())
			if(perk["id"].String() == perkId)
				return perk["effect"]["status"].String();
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
		if(!includeSkillSpecialtyRules)
		{
			auto heroRules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
			heroRules.Struct().erase("skillSpecialties");
			// Preserve an older exact hero-rules snapshot; do not inherit the
			// installed module's optional core-specialty conversion.
			heroRules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));

		legacyPerkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto activePerkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		setPerkStatus(activePerkRules, ROADMASTER_PERK, "active");
		setPerkStatus(activePerkRules, WAYFARER_PERK, "active");
		const auto rapidRegistryStatus = getPerkStatus(activePerkRules, RAPID_EMBARKATION_PERK);
		RecordProperty("rapid_embarkation_registry_status", rapidRegistryStatus);
		ASSERT_EQ(rapidRegistryStatus, "active") << "Rapid Embarkation must be active in the shipped registry";
		const auto mountaineerRegistryStatus = getPerkStatus(activePerkRules, MOUNTAINEER_PERK);
		RecordProperty("mountaineer_registry_status", mountaineerRegistryStatus);
		if(mountaineerRegistryStatus == "planned")
		{
			setPerkStatus(activePerkRules, MOUNTAINEER_PERK, "active");
			RecordProperty("mountaineer_fixture_override", "planned_to_active");
		}
		else if(mountaineerRegistryStatus == "active")
			RecordProperty("mountaineer_fixture_override", "none");
		else
			throw std::runtime_error("Unexpected Mountaineer status in Logistics movement fixture: " + mountaineerRegistryStatus);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(activePerkRules));
	}

	void startGame(const HeroTypeID selectedHeroType = HeroTypeID(16))
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsLogisticsMovementPerks")
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, selectedHeroType, PlayerColor(0)) // Mephala/Kyrre; Rampart native terrain is grass.
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

	void addMovementBonus(const bool water, const BonusSource source, const int value,
		const BonusValueType valueType)
	{
		const auto subtype = BonusSubtypeID(water
			? BonusCustomSubtype::heroMovementSea
			: BonusCustomSubtype::heroMovementLand);
		hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
			source, value, BonusSourceID(), subtype, valueType));
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
		// Simulate an older saved rules snapshot: acquired identities remain, but
		// their then-planned statuses do not activate these movement perks.
		setPerkStatus(legacyPerkRules, ROADMASTER_PERK, "planned");
		setPerkStatus(legacyPerkRules, WAYFARER_PERK, "planned");
		setPerkStatus(legacyPerkRules, MOUNTAINEER_PERK, "planned");
		setPerkStatus(legacyPerkRules, RAPID_EMBARKATION_PERK, "planned");
		auto savedState = hero->getPerkState().toJson();
		savedState["rules"] = legacyPerkRules;
		auto restoredState = newHorizonsHeroes::PerkState::fromJson(savedState);
		const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()) = std::move(restoredState);
	}

	bool includeSkillSpecialtyRules = true;
	JsonNode legacyPerkRules;
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, RapidEmbarkationForecastMatchesAcceptedEmbarkAndDisembark)
{
	startGame();
	advanceLogistics();
	ASSERT_TRUE(chooseOfferedPerk(SCOUTING_PERK));
	advanceLogistics();
	CPathfinderHelper unselected(*gameState(), hero, PathfinderOptions(*gameState()));
	EXPECT_FALSE(unselected.getTurnInfo()->hasNewHorizonsRapidEmbarkation());
	EXPECT_EQ(hero->movementPointsAfterEmbark(200, 10, false, unselected.getTurnInfo()), 0);
	ASSERT_TRUE(chooseOfferedPerk(RAPID_EMBARKATION_PERK));
	const auto shore = hero->visitablePos();
	const auto sea = shore + int3(1, 0, 0);
	setTile(sea, ETerrainId::WATER, RoadId::NO_ROAD);
	handler->createBoat(sea, BoatId::CASTLE, hero->getOwner());
	CPathfinderHelper helper(*gameState(), hero, PathfinderOptions(*gameState()));
	ASSERT_TRUE(helper.getTurnInfo()->hasNewHorizonsRapidEmbarkation());
	ASSERT_FALSE(helper.getTurnInfo()->hasNewHorizonsNavigation());
	const int landMaximum = helper.getTurnInfo()->getMaxMovePoints(EPathfindingLayer::LAND);
	const int seaMaximum = helper.getTurnInfo()->getMaxMovePoints(EPathfindingLayer::SAIL);
	handler->setMovePoints(hero->id, landMaximum);
	const int embarkCost = newHorizonsMovement::rapidEmbarkationCost(landMaximum);
	EXPECT_EQ(helper.getMovementCost(shore, sea, EPathfindingLayer::SAIL, landMaximum, false), embarkCost);
	PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
	const auto paths = cache.getPathsInfo(hero);
	const auto * node = paths->getNode(sea, EPathfindingLayer::SAIL);
	ASSERT_NE(node, nullptr);
	ASSERT_TRUE(node->reachable());
	ASSERT_EQ(node->turns, 0);
	const int predicted = node->moveRemains;
	EXPECT_EQ(predicted, static_cast<int>(static_cast<int64_t>(landMaximum - embarkCost) * seaMaximum / landMaximum));
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(sea),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->movementPointsRemaining(), predicted);
	ASSERT_TRUE(hero->inBoat());
	CPathfinderHelper aboard(*gameState(), hero, PathfinderOptions(*gameState()));
	const int landingCost = newHorizonsMovement::rapidEmbarkationCost(seaMaximum);
	EXPECT_EQ(aboard.getMovementCost(sea, shore, EPathfindingLayer::LAND, predicted, false), landingCost);
	PathfinderCache landingCache(gameState().get(), PathfinderOptions(*gameState()));
	const auto landingPaths = landingCache.getPathsInfo(hero);
	const auto * landing = landingPaths->getNode(shore, EPathfindingLayer::LAND);
	ASSERT_NE(landing, nullptr);
	ASSERT_TRUE(landing->reachable());
	ASSERT_EQ(landing->turns, 0);
	const int predictedLanding = landing->moveRemains;
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(shore),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	EXPECT_EQ(hero->movementPointsRemaining(), predictedLanding);
	EXPECT_EQ(predictedLanding, static_cast<int>(static_cast<int64_t>(predicted - landingCost) * landMaximum / seaMaximum));
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, RapidEmbarkationUsesFinalCostWithNavigationAndPreservesFreeBoarding)
{
	startGame();
	advanceLogistics();
	ASSERT_TRUE(chooseOfferedPerk(NAVIGATION_PERK));
	advanceLogistics();
	ASSERT_TRUE(chooseOfferedPerk(RAPID_EMBARKATION_PERK));
	const auto shore = hero->visitablePos();
	const auto sea = shore + int3(1, 0, 0);
	setTile(sea, ETerrainId::WATER, RoadId::NO_ROAD);
	handler->createBoat(sea, BoatId::CASTLE, hero->getOwner());
	CPathfinderHelper helper(*gameState(), hero, PathfinderOptions(*gameState()));
	const auto * info = helper.getTurnInfo();
	ASSERT_TRUE(info->hasNewHorizonsNavigation());
	ASSERT_TRUE(info->hasNewHorizonsRapidEmbarkation());
	const int landMaximum = info->getMaxMovePoints(EPathfindingLayer::LAND);
	const int seaMaximum = info->getMaxMovePoints(EPathfindingLayer::SAIL);
	const int cost = newHorizonsMovement::rapidEmbarkationCost(landMaximum);
	EXPECT_EQ(helper.getMovementCost(shore, sea, EPathfindingLayer::SAIL, landMaximum, false), cost);
	EXPECT_EQ(hero->movementPointsAfterEmbark(landMaximum, cost, false, info),
		static_cast<int>(static_cast<int64_t>(landMaximum - cost) * seaMaximum / landMaximum));
	handler->setMovePoints(hero->id, cost - 1);
	ASSERT_FALSE(handler->moveHero(hero->id, hero->convertFromVisitablePos(sea),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_FALSE(hero->inBoat());
	EXPECT_EQ(hero->movementPointsRemaining(), cost - 1);
	hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FREE_SHIP_BOARDING, BonusSource::OTHER, 1, BonusSourceID()));
	CPathfinderHelper freeBoarding(*gameState(), hero, PathfinderOptions(*gameState()));
	const int freeCost = freeBoarding.getMovementCost(shore, sea, EPathfindingLayer::SAIL, landMaximum, false);
	EXPECT_EQ(freeCost, 5) << "Free boarding preserves the ordinary 10-point step, halved by Navigation";
	EXPECT_EQ(hero->movementPointsAfterEmbark(landMaximum, freeCost, false, freeBoarding.getTurnInfo()),
		static_cast<int>(static_cast<int64_t>(landMaximum - freeCost) * seaMaximum / landMaximum));
	installLegacyPlannedSnapshot();
	CPathfinderHelper legacy(*gameState(), hero, PathfinderOptions(*gameState()));
	EXPECT_FALSE(legacy.getTurnInfo()->hasNewHorizonsRapidEmbarkation());
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

TEST_F(NewHorizonsLogisticsMovementPerksTest, MountaineerRemovesOnlyRoughAndSubterraneanPenalties)
{
	startGame();
	const auto source = hero->visitablePos();
	const auto roughDestination = source + int3(1, 0, 0);
	setTile(source, ETerrainId::ROUGH, RoadId::NO_ROAD);
	setTile(roughDestination, ETerrainId::ROUGH, RoadId::NO_ROAD);

	advanceLogistics(); // Basic Logistics.
	ASSERT_TRUE(chooseOfferedPerk(PATHFINDING_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, PATHFINDING_PERK));
	advanceLogistics(); // Advanced Logistics, legally unlocked by Basic Pathfinding.
	ASSERT_FALSE(hero->hasActivePerk(LOGISTICS_SKILL, MOUNTAINEER_PERK));
	EXPECT_EQ(forecastCost(roughDestination), 12); // Pathfinding halves the ordinary rough-terrain surcharge.
	ASSERT_TRUE(chooseOfferedPerk(MOUNTAINEER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, MOUNTAINEER_PERK));
	expectAcceptedMove(roughDestination, 10);

	const auto subterraneanSource = hero->visitablePos();
	const auto subterraneanDestination = subterraneanSource + int3(1, 0, 0);
	const auto unrelatedDestination = subterraneanSource + int3(0, 1, 0);
	setTile(subterraneanSource, ETerrainId::LAVA, RoadId::NO_ROAD);
	setTile(unrelatedDestination, ETerrainId::LAVA, RoadId::NO_ROAD);
	EXPECT_EQ(forecastCost(unrelatedDestination), 12); // Mountaineer does not waive other terrain penalties.

	setTile(subterraneanSource, ETerrainId::SUBTERRANEAN, RoadId::NO_ROAD);
	setTile(subterraneanDestination, ETerrainId::SUBTERRANEAN, RoadId::NO_ROAD);
	EXPECT_EQ(forecastCost(subterraneanDestination), 10);
	expectAcceptedMove(subterraneanDestination, 10);

	installLegacyPlannedSnapshot();
	EXPECT_FALSE(hero->hasActivePerk(LOGISTICS_SKILL, MOUNTAINEER_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, PATHFINDING_PERK));
	const auto plannedDestination = hero->visitablePos() + int3(1, 0, 0);
	setTile(hero->visitablePos(), ETerrainId::SUBTERRANEAN, RoadId::NO_ROAD);
	setTile(plannedDestination, ETerrainId::SUBTERRANEAN, RoadId::NO_ROAD);
	expectAcceptedMove(plannedDestination, 12); // A saved planned perk does not suppress the surcharge.

	const auto rock = hero->visitablePos() + int3(1, 0, 0);
	setTile(rock, ETerrainId::ROCK, RoadId::NO_ROAD);
	const auto positionBeforeBlockedMove = hero->visitablePos();
	const int movementBeforeBlockedMove = hero->movementPointsRemaining();
	EXPECT_FALSE(handler->moveHero(hero->id, hero->convertFromVisitablePos(rock),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), positionBeforeBlockedMove);
	EXPECT_EQ(hero->movementPointsRemaining(), movementBeforeBlockedMove);
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, CurrentDailyMovementBreakdownMatchesLiveLandAndSeaComponentsAndBounds)
{
	startGame();
	EXPECT_FALSE(newHorizonsMovement::currentDailyMovementBreakdown(nullptr, false).has_value());

	const auto initialLand = newHorizonsMovement::currentDailyMovementBreakdown(hero, false);
	const auto initialSea = newHorizonsMovement::currentDailyMovementBreakdown(hero, true);
	ASSERT_TRUE(initialLand.has_value());
	ASSERT_TRUE(initialSea.has_value());
	EXPECT_EQ(initialLand->limit, 200);
	EXPECT_EQ(initialSea->limit, 200);
	EXPECT_EQ(initialLand->percentageToBase, 0);
	EXPECT_EQ(initialSea->percentageToBase, 0);

	// Attach ordinary bonuses to the real hero to exercise every displayed
	// component, source-percentage modification, and the live minimum/maximum.
	addMovementBonus(false, BonusSource::ARTIFACT, 10, BonusValueType::PERCENT_TO_SOURCE);
	addMovementBonus(false, BonusSource::ARTIFACT, 10, BonusValueType::BASE_NUMBER);
	addMovementBonus(false, BonusSource::ARTIFACT, 20, BonusValueType::PERCENT_TO_BASE);
	addMovementBonus(false, BonusSource::SPELL_EFFECT, 5, BonusValueType::ADDITIVE_VALUE);
	addMovementBonus(false, BonusSource::SPELL_EFFECT, 5, BonusValueType::PERCENT_TO_ALL);
	addMovementBonus(false, BonusSource::OTHER, 300, BonusValueType::INDEPENDENT_MAX);
	addMovementBonus(false, BonusSource::OTHER, 320, BonusValueType::INDEPENDENT_MIN);

	addMovementBonus(true, BonusSource::OTHER, 20, BonusValueType::BASE_NUMBER);
	addMovementBonus(true, BonusSource::OTHER, 10, BonusValueType::PERCENT_TO_BASE);
	addMovementBonus(true, BonusSource::SPELL_EFFECT, 8, BonusValueType::ADDITIVE_VALUE);
	addMovementBonus(true, BonusSource::SPELL_EFFECT, 5, BonusValueType::PERCENT_TO_ALL);
	addMovementBonus(true, BonusSource::OTHER, 250, BonusValueType::INDEPENDENT_MAX);
	addMovementBonus(true, BonusSource::OTHER, 260, BonusValueType::INDEPENDENT_MIN);

	const auto land = newHorizonsMovement::currentDailyMovementBreakdown(hero, false);
	const auto sea = newHorizonsMovement::currentDailyMovementBreakdown(hero, true);
	ASSERT_TRUE(land.has_value());
	ASSERT_TRUE(sea.has_value());
	EXPECT_EQ(land->baseAdjustment, 11); // Artifact source +10% modifies its +10 base value.
	EXPECT_EQ(land->percentageToBase, 22);
	EXPECT_EQ(land->flat, 5);
	EXPECT_EQ(land->percentageToAll, 5);
	ASSERT_TRUE(land->lowerBound.has_value());
	ASSERT_TRUE(land->upperBound.has_value());
	EXPECT_EQ(*land->lowerBound, 300);
	EXPECT_EQ(*land->upperBound, 320);
	EXPECT_EQ(land->limit, 300); // Unbounded result 275 is raised by the minimum.

	EXPECT_EQ(sea->baseAdjustment, 20);
	EXPECT_EQ(sea->percentageToBase, 10);
	EXPECT_EQ(sea->flat, 8);
	EXPECT_EQ(sea->percentageToAll, 5);
	ASSERT_TRUE(sea->lowerBound.has_value());
	ASSERT_TRUE(sea->upperBound.has_value());
	EXPECT_EQ(*sea->lowerBound, 250);
	EXPECT_EQ(*sea->upperBound, 260);
	EXPECT_EQ(sea->limit, 260); // Unbounded result 262 is capped by the maximum.

	TurnInfoCache movementCache(hero);
	const TurnInfo liveMovement(&movementCache, hero, 0);
	EXPECT_EQ(liveMovement.getMovePointsLimitLand(), land->limit);
	EXPECT_EQ(liveMovement.getMovePointsLimitWater(), sea->limit);
	EXPECT_EQ(hero->movementPointsLimit(), land->limit);
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, CurrentDailyMovementBreakdownIncludesCoreLogisticsSpecialtyAndNavigation)
{
	startGame(heroType("core:kyrre"));
	// TinyMap hero placement does not apply Kyrre's authored starting skills;
	// install her Basic New Horizons Logistics through the game handler, as in
	// the dedicated skill-specialty fixture.
	handler->changeSecSkill(hero, logisticsSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const SecondarySkill coreLogistics(SecondarySkill::LOGISTICS);
	EXPECT_EQ(hero->getSkillSpecialtyCoreBonusPercent(coreLogistics), 20);

	const auto initialLand = newHorizonsMovement::currentDailyMovementBreakdown(hero, false);
	const auto initialSea = newHorizonsMovement::currentDailyMovementBreakdown(hero, true);
	ASSERT_TRUE(initialLand.has_value());
	ASSERT_TRUE(initialSea.has_value());
	EXPECT_EQ(initialLand->percentageToBase, 12); // Core Logistics 10% receives the supported +20% specialty.
	EXPECT_EQ(initialSea->percentageToBase, 12);
	EXPECT_EQ(initialLand->limit, 224);
	EXPECT_EQ(initialSea->limit, 224);

	ASSERT_TRUE(chooseOfferedPerk(NAVIGATION_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, NAVIGATION_PERK));
	const auto land = newHorizonsMovement::currentDailyMovementBreakdown(hero, false);
	const auto sea = newHorizonsMovement::currentDailyMovementBreakdown(hero, true);
	ASSERT_TRUE(land.has_value());
	ASSERT_TRUE(sea.has_value());
	EXPECT_EQ(land->percentageToBase, 12);
	EXPECT_EQ(land->limit, 224);
	EXPECT_EQ(sea->percentageToBase, 37); // Navigation is added only to the sea percentage-to-base stage.
	EXPECT_EQ(sea->limit, 274);

	TurnInfoCache movementCache(hero);
	const TurnInfo liveMovement(&movementCache, hero, 0);
	EXPECT_EQ(liveMovement.getMovePointsLimitLand(), land->limit);
	EXPECT_EQ(liveMovement.getMovePointsLimitWater(), sea->limit);
}

TEST_F(NewHorizonsLogisticsMovementPerksTest, CurrentDailyMovementBreakdownKeepsLegacySpecialtyAndNullGuards)
{
	includeSkillSpecialtyRules = false;
	startGame(heroType("core:kyrre"));
	handler->changeSecSkill(hero, logisticsSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const SecondarySkill coreLogistics(SecondarySkill::LOGISTICS);
	EXPECT_EQ(hero->getSkillSpecialtyCoreBonusPercent(coreLogistics), 0);

	const auto legacyLand = newHorizonsMovement::currentDailyMovementBreakdown(hero, false);
	const auto legacySea = newHorizonsMovement::currentDailyMovementBreakdown(hero, true);
	ASSERT_TRUE(legacyLand.has_value());
	ASSERT_TRUE(legacySea.has_value());
	EXPECT_EQ(legacyLand->percentageToBase, 10); // The preserved legacy target-type alias rounds down at Kyrre's starting level.
	EXPECT_EQ(legacySea->percentageToBase, 10);
	EXPECT_EQ(legacyLand->limit, 220);
	EXPECT_EQ(legacySea->limit, 220);
	EXPECT_FALSE(newHorizonsMovement::currentDailyMovementBreakdown(nullptr, false).has_value());

	TurnInfoCache movementCache(hero);
	const TurnInfo liveMovement(&movementCache, hero, 0);
	EXPECT_EQ(liveMovement.getMovePointsLimitLand(), legacyLand->limit);
	EXPECT_EQ(liveMovement.getMovePointsLimitWater(), legacySea->limit);
}
