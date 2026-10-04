/*
 * AdventureSpellDailyPathfindingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Pathfinding/Actions/AdventureSpellCastMovementActions.h"
#include "AI/Nullkiller2/Pathfinding/Actions/BoatActions.h"
#include "AI/Nullkiller2/Pathfinding/Actions/DimensionDoorAction.h"
#include "AI/Nullkiller2/Pathfinding/Actions/TownPortalAction.h"
#include "AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "SpellPointTestUtils.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/GameLibrary.h"
#include "lib/GameConstants.h"
#include "lib/IGameSettings.h"
#include "lib/CPlayerState.h"
#include "lib/bonuses/Bonus.h"
#include "lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/MiscObjects.h"
#include "lib/modding/CModHandler.h"
#include "lib/pathfinder/PathfinderOptions.h"
#include "lib/spells/NewHorizonsMagic.h"
#include "lib/spells/CSpell.h"

namespace
{
const PlayerColor PLAYER(0);

SpellID spell(const char * identity)
{
	return SpellID(SpellID::decode(identity));
}

template<typename TAction>
bool hasAvailableActionOnTurn(const NK2AI::AIPath & path, uint8_t turn)
{
	return std::ranges::any_of(path.nodes, [turn](const NK2AI::AIPathNodeInfo & node)
	{
		return node.turns == turn
			&& node.specialAction
			&& dynamic_cast<const TAction *>(node.specialAction.get())
			&& !node.actionIsBlocked;
	});
}

class AdventureSpellDailyPathfindingTest : public NullkillerTest
{
protected:
	static constexpr int MOVEMENT_POINTS_BELOW_ONE_STEP = 1;

	bool useNewHorizonsRules = true;

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(useNewHorizonsRules)
		{
			JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
			newHorizonsMagic::validateRules(magicRules);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		}
		else
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	CGHeroInstance * startHeroWithSpells(bool newHorizons, std::vector<SpellID> heroSpells)
	{
		useNewHorizonsRules = newHorizons;

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.name("AdventureSpellDailyPathfinding")
			.playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.heroGarrison({{CreatureID(27), 1}})
			.heroPrimary(10, 10, 10, 20)
			.heroSpells(std::move(heroSpells))
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));

		auto * hero = findHeroByOwner(PLAYER);
		if(hero)
		{
			setTestSpellPointTotal(hero, 200);
			hero->setMovementPoints(2000);
		}
		return hero;
	}

	CGHeroInstance * startHero(bool newHorizons)
	{
		return startHeroWithSpells(newHorizons, {
			spell("core:fly"),
			spell("core:waterWalk"),
			spell("core:summonBoat"),
			spell("core:dimensionDoor"),
			spell("core:townPortal")});
	}

	std::vector<NK2AI::AIPath> pathsTo(NK2AI::AIGateway & gateway, const CGHeroInstance * hero, const int3 & target)
	{
		NK2AI::HeroMap<NK2AI::HeroRole> heroes;
		heroes.emplace(hero, NK2AI::MAIN);
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		gateway.nullkiller->pathfinder->updatePaths(heroes, settings);
		return gateway.nullkiller->pathfinder->getPathInfo(target);
	}

	void surroundWithWater(const int3 & center)
	{
		for(int dx = -1; dx <= 1; ++dx)
		{
			for(int dy = -1; dy <= 1; ++dy)
			{
				if(dx == 0 && dy == 0)
					continue;

				gameState()->getMap().getTile(center + int3(dx, dy, 0)).terrainType = ETerrainId::WATER;
			}
		}
	}

	void makeRockWall(int x)
	{
		for(int y = 0; y < map()->height; ++y)
			map()->getTile({x, y, 0}).terrainType = ETerrainId::ROCK;
	}

	void setTileVisibility(const int3 & tile, bool visible)
	{
		const auto teamId = gameState()->players.at(PLAYER).team;
		gameState()->teams.at(teamId).fogOfWarMap[tile] = visible ? 1 : 0;
	}

	void addOneDayTravelBonus(CGHeroInstance * hero, BonusType type)
	{
		hero->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::ONE_DAY, type, BonusSource::SPELL_EFFECT, 40, BonusSourceID()));
	}

	void makeSummonBoatSuccessCertain(CGHeroInstance * hero)
	{
		const auto summonBoat = spell("core:summonBoat");
		hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(summonBoat)));
	}

	void addAvailableBoat(const int3 & position)
	{
		const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::BOAT, BoatId::NECROPOLIS);
		const auto templates = handler->getTemplates();
		ASSERT_FALSE(templates.empty());
		auto boat = handler->create(gameState().get(), templates.front());
		ASSERT_NE(boat, nullptr);
		boat->setAnchorPos(position);
		map()->generateUniqueInstanceName(boat.get());
		map()->addNewObject(std::move(boat));
	}
};
}

TEST_F(AdventureSpellDailyPathfindingTest, SharedCastActionsBlockChainsTodayAndResetForFutureTurn)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(newHorizonsMagic::adventureSpellRulesActive(hero->getMagicRules()));

	const SpellID flySpell = spell("core:fly");
	const SpellID waterWalkSpell = spell("core:waterWalk");
	NK2AI::ChainActor actor;
	actor.hero = hero;

	NK2AI::AIPathNode sourceNode;
	sourceNode.actor = &actor;
	sourceNode.turns = 0;
	sourceNode.dayFlags = NK2AI::DayFlags::NONE;
	NK2AI::AIPathNode flyNode;
	flyNode.actor = &actor;
	PathNodeInfo source;
	source.node = &sourceNode;
	CDestinationNodeInfo destination;
	destination.turn = 0;

	NK2AI::AIPathfinding::AirWalkingAction fly(hero, flySpell);
	NK2AI::AIPathfinding::WaterWalkingAction waterWalk(hero, waterWalkSpell);
	ASSERT_TRUE(fly.usesNewHorizonsAdventureSpellOpportunity());
	ASSERT_TRUE(waterWalk.usesNewHorizonsAdventureSpellOpportunity());
	ASSERT_TRUE(fly.canAct(nullptr, &sourceNode, 0));

	fly.applyOnDestination(hero, destination, source, &flyNode, &sourceNode);
	flyNode.turns = 0;
	EXPECT_NE(flyNode.dayFlags & NK2AI::DayFlags::FLY_CAST, NK2AI::DayFlags::NONE);
	EXPECT_NE(flyNode.dayFlags & NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST, NK2AI::DayFlags::NONE);
	EXPECT_FALSE(waterWalk.canAct(nullptr, &flyNode, 0));
	EXPECT_TRUE(waterWalk.canAct(nullptr, &flyNode, 1));
}

TEST_F(AdventureSpellDailyPathfindingTest, InitialSpentFlagBlocksAllOtherAdventureActionsToday)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	hero->setNewHorizonsAdventureSpellCastToday(true);

	NK2AI::ChainActor actor;
	actor.hero = hero;
	NK2AI::AIPathNode source;
	source.actor = &actor;
	source.turns = 0;
	source.dayFlags = NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST;
	const SpellID flySpell = spell("core:fly");
	NK2AI::AIPathfinding::AirWalkingAction fly(hero, flySpell);
	EXPECT_FALSE(fly.canAct(nullptr, &source, 0));
	EXPECT_TRUE(fly.canAct(nullptr, &source, 1));

	NK2AI::AIPathfinding::SummonBoatAction summonBoat(spell("core:summonBoat"), true);
	EXPECT_FALSE(summonBoat.canAct(nullptr, &source, 0));
	EXPECT_TRUE(summonBoat.canAct(nullptr, &source, 1));

	NK2AI::AIPathfinding::TownPortalAction townPortal(nullptr, spell("core:townPortal"), true);
	EXPECT_FALSE(townPortal.canAct(nullptr, &source, 0));
	EXPECT_TRUE(townPortal.canAct(nullptr, &source, 1));
}

TEST_F(AdventureSpellDailyPathfindingTest, NewHorizonsSummonBoatDoesNotForecastCreatingAnUnavailableBoat)
{
	const SpellID summonBoatSpell = spell("core:summonBoat");
	auto * hero = startHeroWithSpells(true, {summonBoatSpell});
	ASSERT_NE(hero, nullptr);
	makeSummonBoatSuccessCertain(hero);

	const int3 source = hero->visitablePos();
	surroundWithWater(source);
	const int3 target = source + int3(0, -1, 0);
	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);

	EXPECT_FALSE(std::ranges::any_of(paths, [](const NK2AI::AIPath & path)
	{
		return hasAvailableActionOnTurn<NK2AI::AIPathfinding::SummonBoatAction>(path, 0);
	}));
}

TEST_F(AdventureSpellDailyPathfindingTest, NewHorizonsSummonBoatPathUsesAKnownAvailableBoat)
{
	const SpellID summonBoatSpell = spell("core:summonBoat");
	auto * hero = startHeroWithSpells(true, {summonBoatSpell});
	ASSERT_NE(hero, nullptr);
	makeSummonBoatSuccessCertain(hero);

	const int3 source = hero->visitablePos();
	surroundWithWater(source);
	const int3 boatPosition = source + int3(2, 1, 0);
	map()->getTile(boatPosition).terrainType = ETerrainId::WATER;
	ASSERT_NO_FATAL_FAILURE(addAvailableBoat(boatPosition));
	const int3 target = source + int3(0, -1, 0);
	const auto gateway = makeGateway(PLAYER);
	const auto boats = gameState()->getMap().getObjects<CGBoat>();
	ASSERT_EQ(boats.size(), 1u);
	ASSERT_TRUE(vstd::contains(gateway->nullkiller->memory->visitableObjs, boats.front()->id));
	const auto paths = pathsTo(*gateway, hero, target);

	EXPECT_TRUE(std::ranges::any_of(paths, [](const NK2AI::AIPath & path)
	{
		return hasAvailableActionOnTurn<NK2AI::AIPathfinding::SummonBoatAction>(path, 0);
	}));
}

TEST_F(AdventureSpellDailyPathfindingTest, NewHorizonsDimensionDoorPathExhaustsMovementBeforeTheFollowingTile)
{
	const SpellID dimensionDoorSpell = spell("core:dimensionDoor");
	auto * hero = startHeroWithSpells(true, {dimensionDoorSpell});
	ASSERT_NE(hero, nullptr);
	ASSERT_GT(hero->movementPointsRemaining(), 300);
	setMapVisibility(PLAYER, true);

	const int3 source = hero->visitablePos();
	makeRockWall(source.x + 1);
	const int3 target = source + int3(10, 0, 0);
	ASSERT_TRUE(map()->isInTheMap(target));

	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);
	const auto route = std::ranges::find_if(paths, [&](const NK2AI::AIPath & path)
	{
		return path.targetTile() == target;
	});
	ASSERT_NE(route, paths.end()) << "no path crosses the wall through a legal Dimension Door landing";
	ASSERT_TRUE(hasAvailableActionOnTurn<NK2AI::AIPathfinding::DimensionDoorAction>(*route, 0)) << route->toString();
	EXPECT_EQ(route->targetNode().turns, 1) << route->toString();

	bool foundDimensionDoorLanding = false;
	for(size_t index = 0; index < route->nodes.size(); ++index)
	{
		const auto & node = route->nodes[index];
		if(!node.specialAction || !dynamic_cast<const NK2AI::AIPathfinding::DimensionDoorAction *>(node.specialAction.get()))
			continue;

		foundDimensionDoorLanding = true;
		ASSERT_LT(index + 1, route->nodes.size()) << route->toString();
		const auto & sourceNode = route->nodes[index + 1];
		EXPECT_EQ(node.coord.z, sourceNode.coord.z);
		EXPECT_LE(sourceNode.coord.dist(node.coord, int3::DIST_2D), 8u);
		EXPECT_TRUE(gameState()->isVisibleFor(node.coord, PLAYER));
	}
	EXPECT_TRUE(foundDimensionDoorLanding) << route->toString();
}

TEST_F(AdventureSpellDailyPathfindingTest, NewHorizonsDimensionDoorPathDoesNotCrossByTargetingHiddenLandings)
{
	const SpellID dimensionDoorSpell = spell("core:dimensionDoor");
	auto * hero = startHeroWithSpells(true, {dimensionDoorSpell});
	ASSERT_NE(hero, nullptr);

	const int3 source = hero->visitablePos();
	makeRockWall(source.x + 1);
	setMapVisibility(PLAYER, false);
	setTileVisibility(source, true);
	const int3 target = source + int3(10, 0, 0);
	setTileVisibility(target, true);
	ASSERT_TRUE(map()->isInTheMap(target));
	ASSERT_TRUE(gameState()->isVisibleFor(target, PLAYER));

	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);
	EXPECT_FALSE(std::ranges::any_of(paths, [&](const NK2AI::AIPath & path)
	{
		return path.targetTile() == target
			&& std::ranges::any_of(path.nodes, [&](const NK2AI::AIPathNodeInfo & node)
			{
				return node.specialAction
					&& dynamic_cast<const NK2AI::AIPathfinding::DimensionDoorAction *>(node.specialAction.get());
			});
	})) << "a New Horizons route must not reveal or target hidden intermediate landing tiles";
	EXPECT_TRUE(paths.empty()) << "the rock wall prevents a non-spell route to the visible destination";
}

TEST_F(AdventureSpellDailyPathfindingTest, DimensionDoorRevalidationUsesThePlannedSpellDay)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	NK2AI::ChainActor actor;
	actor.hero = hero;

	NK2AI::AIPathNode source;
	source.actor = &actor;
	source.turns = 0;
	source.dayFlags = NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST;

	auto gateway = makeGateway(PLAYER);
	NK2AI::AIPathfinding::DimensionDoorActionParameters parameters;
	parameters.usedSpell = spell("core:dimensionDoor");
	parameters.destination = int3(8, 8, 0);
	parameters.manaCost = hero->getSpellCost(parameters.usedSpell.toSpell());
	parameters.movementPointsRequired = 100;
	parameters.movementPointsTaken = 300;
	parameters.plannedSourceTurn = 0;
	parameters.plannedSourceMoveRemains = 1000;
	parameters.plannedSourceMoveLimit = 1000;
	parameters.usesNewHorizonsAdventureSpellOpportunity = true;
	NK2AI::AIPathfinding::DimensionDoorAction today(parameters);
	EXPECT_FALSE(today.canAct(gateway->nullkiller.get(), &source, 0));

	parameters.plannedSourceTurn = 1;
	NK2AI::AIPathfinding::DimensionDoorAction tomorrow(parameters);
	EXPECT_TRUE(tomorrow.canAct(gateway->nullkiller.get(), &source, 1));
}

TEST_F(AdventureSpellDailyPathfindingTest, BoatAndTownPortalActionsReserveManaAndMarkTheSharedOpportunity)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	NK2AI::ChainActor actor;
	actor.hero = hero;

	NK2AI::AIPathNode sourceNode;
	sourceNode.actor = &actor;
	sourceNode.turns = 0;
	sourceNode.manaCost = 5;
	PathNodeInfo source;
	source.node = &sourceNode;
	CDestinationNodeInfo destination;
	destination.turn = 0;

	NK2AI::AIPathfinding::TownPortalAction townPortal(nullptr, spell("core:townPortal"), true);
	NK2AI::AIPathNode townPortalNode;
	townPortal.applyOnDestination(hero, destination, source, &townPortalNode, &sourceNode);
	EXPECT_EQ(townPortalNode.manaCost, 5 + hero->getSpellCost(spell("core:townPortal").toSpell()));
	EXPECT_NE(townPortalNode.dayFlags & NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST, NK2AI::DayFlags::NONE);

	NK2AI::AIPathfinding::SummonBoatAction summonBoat(spell("core:summonBoat"), true);
	NK2AI::AIPathNode boatNode;
	summonBoat.applyOnDestination(hero, destination, source, &boatNode, &sourceNode);
	EXPECT_EQ(boatNode.manaCost, 5 + hero->getSpellCost(spell("core:summonBoat").toSpell()));
	EXPECT_NE(boatNode.dayFlags & NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST, NK2AI::DayFlags::NONE);
}

TEST_F(AdventureSpellDailyPathfindingTest, RolloverUsesCanonicalUnspentNodeWithoutOverwritingSettledRoutes)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	auto gateway = makeGateway(PLAYER);
	NK2AI::ChainActor actor;
	actor.hero = hero;
	const int3 target = hero->visitablePos() + int3(1, 0, 0);

	for(const bool settled : {false, true})
	{
		NK2AI::AINodeStorage storage(gateway->nullkiller.get(), gameState()->getMapSize());
		storage.clear();
		storage.setHeroes({});
		storage.initialize(PathfinderOptions(*gameState()), *gameState());
		auto used = storage.getOrCreateNode(target, EPathfindingLayer::LAND, &actor,
			NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST);
		auto unspent = storage.getOrCreateNode(target, EPathfindingLayer::LAND, &actor, NK2AI::DayFlags::NONE);
		ASSERT_TRUE(used);
		ASSERT_TRUE(unspent);
		ASSERT_NE(*used, *unspent);
		(*unspent)->turns = 1;
		(*unspent)->setCost(9.0f);
		(*unspent)->locked = settled;

		NK2AI::AIPathNode sourceNode;
		sourceNode.actor = &actor;
		sourceNode.turns = 0;
		sourceNode.dayFlags = NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST;
		PathNodeInfo source;
		source.node = &sourceNode;
		CDestinationNodeInfo destination;
		destination.node = *used;
		destination.coord = target;
		destination.turn = 1;
		destination.cost = 1.1f;
		destination.movementLeft = 100;
		destination.action = EPathNodeAction::NORMAL;
		storage.commit(destination, source);

		EXPECT_EQ(destination.node, *unspent);
		EXPECT_EQ(destination.blocked, settled);
		EXPECT_EQ((*used)->dayFlags, NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST);
		EXPECT_EQ((*unspent)->dayFlags, NK2AI::DayFlags::NONE);
		EXPECT_FLOAT_EQ((*unspent)->getCost(), settled ? 9.0f : 1.1f);
		EXPECT_EQ(storage.getOrCreateNode(target, EPathfindingLayer::LAND, &actor, NK2AI::DayFlags::NONE), unspent);
	}
}

TEST_F(AdventureSpellDailyPathfindingTest, LowMovementRolloverCanUseTomorrowWaterWalkAfterAllowanceWasSpentToday)
{
	const SpellID waterWalkSpell = spell("core:waterWalk");
	auto * hero = startHeroWithSpells(true, {waterWalkSpell});
	ASSERT_NE(hero, nullptr);
	hero->setMovementPoints(MOVEMENT_POINTS_BELOW_ONE_STEP);
	hero->setNewHorizonsAdventureSpellCastToday(true);

	const int3 source = hero->visitablePos();
	surroundWithWater(source);
	const int3 target = source + int3(2, 0, 0);
	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);
	const auto route = std::ranges::find_if(paths, [hero, target](const NK2AI::AIPath & path)
	{
		return path.targetHero == hero && path.targetTile() == target;
	});

	ASSERT_NE(route, paths.end()) << "AI should defer the water crossing until movement replenishes tomorrow";
	EXPECT_TRUE((hasAvailableActionOnTurn<NK2AI::AIPathfinding::WaterWalkingAction>(*route, 1))) << route->toString();
}

TEST_F(AdventureSpellDailyPathfindingTest, OneDayWaterWalkExpiresBeforeLowMovementCrossingTomorrow)
{
	const SpellID waterWalkSpell = spell("core:waterWalk");
	auto * hero = startHeroWithSpells(true, {waterWalkSpell});
	ASSERT_NE(hero, nullptr);
	addOneDayTravelBonus(hero, BonusType::WATER_WALKING);
	hero->setNewHorizonsAdventureSpellCastToday(true);
	hero->setMovementPoints(MOVEMENT_POINTS_BELOW_ONE_STEP);

	const int3 source = hero->visitablePos();
	surroundWithWater(source);
	const int3 target = source + int3(2, 0, 0);
	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);
	const auto route = std::ranges::find_if(paths, [hero, target](const NK2AI::AIPath & path)
	{
		return path.targetHero == hero && path.targetTile() == target;
	});

	ASSERT_NE(route, paths.end()) << "AI should not carry today's one-day Water Walk into tomorrow";
	EXPECT_TRUE((hasAvailableActionOnTurn<NK2AI::AIPathfinding::WaterWalkingAction>(*route, 1))) << route->toString();
}

TEST_F(AdventureSpellDailyPathfindingTest, OneDayFlyExpiresBeforeLowMovementCrossingTomorrow)
{
	const SpellID flySpell = spell("core:fly");
	auto * hero = startHeroWithSpells(true, {flySpell});
	ASSERT_NE(hero, nullptr);
	addOneDayTravelBonus(hero, BonusType::FLYING_MOVEMENT);
	hero->setNewHorizonsAdventureSpellCastToday(true);
	hero->setMovementPoints(MOVEMENT_POINTS_BELOW_ONE_STEP);

	const int3 source = hero->visitablePos();
	surroundWithWater(source);
	const int3 target = source + int3(2, 0, 0);
	const auto gateway = makeGateway(PLAYER);
	const auto paths = pathsTo(*gateway, hero, target);
	const auto route = std::ranges::find_if(paths, [hero, target](const NK2AI::AIPath & path)
	{
		return path.targetHero == hero && path.targetTile() == target;
	});

	ASSERT_NE(route, paths.end()) << "AI should recast Fly when its one-day effect expires before tomorrow's movement";
	EXPECT_TRUE((hasAvailableActionOnTurn<NK2AI::AIPathfinding::AirWalkingAction>(*route, 1))) << route->toString();
}

TEST_F(AdventureSpellDailyPathfindingTest, RolloverRekeysLockedProvisionalNodeBeforeCheckingItsCanonicalTomorrowNode)
{
	auto * hero = startHero(true);
	ASSERT_NE(hero, nullptr);
	const auto gateway = makeGateway(PLAYER);
	NK2AI::ChainActor actor;
	actor.hero = hero;
	const int3 target = hero->visitablePos() + int3(1, 0, 0);

	NK2AI::AINodeStorage storage(gateway->nullkiller.get(), gameState()->getMapSize());
	storage.clear();
	storage.setHeroes({});
	storage.initialize(PathfinderOptions(*gameState()), *gameState());
	const auto usedToday = storage.getOrCreateNode(
		target, EPathfindingLayer::LAND, &actor, NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST);
	const auto availableTomorrow = storage.getOrCreateNode(target, EPathfindingLayer::LAND, &actor, NK2AI::DayFlags::NONE);
	ASSERT_TRUE(usedToday);
	ASSERT_TRUE(availableTomorrow);
	ASSERT_NE(*usedToday, *availableTomorrow);

	(*usedToday)->locked = true;
	(*usedToday)->specialAction = std::make_shared<NK2AI::AIPathfinding::WaterWalkingAction>(
		hero, spell("core:waterWalk"));
	NK2AI::AIPathNode sourceNode;
	sourceNode.actor = &actor;
	sourceNode.turns = 0;
	sourceNode.dayFlags = NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST;
	PathNodeInfo source;
	source.node = &sourceNode;
	CDestinationNodeInfo destination;
	destination.node = *usedToday;
	destination.turn = 1;
	destination.blocked = false;

	storage.prepareDestination(destination, source);

	EXPECT_EQ(destination.node, *availableTomorrow);
	EXPECT_FALSE(destination.blocked);
	EXPECT_TRUE((*usedToday)->locked);
	ASSERT_NE((*usedToday)->specialAction, nullptr);
	EXPECT_EQ((*availableTomorrow)->specialAction, nullptr);
}

TEST_F(AdventureSpellDailyPathfindingTest, LegacyWalkCastDoesNotConsumeSharedNewHorizonsFlag)
{
	auto * hero = startHero(false);
	ASSERT_NE(hero, nullptr);
	NK2AI::ChainActor actor;
	actor.hero = hero;
	NK2AI::AIPathNode source;
	source.actor = &actor;
	source.turns = 0;

	NK2AI::AIPathfinding::AirWalkingAction fly(hero, spell("core:fly"));
	EXPECT_FALSE(fly.usesNewHorizonsAdventureSpellOpportunity());
	EXPECT_TRUE(fly.canAct(nullptr, &source, 0));

	NK2AI::AIPathNode destinationNode;
	PathNodeInfo sourceInfo;
	sourceInfo.node = &source;
	CDestinationNodeInfo destination;
	destination.turn = 0;
	fly.applyOnDestination(hero, destination, sourceInfo, &destinationNode, &source);
	EXPECT_NE(destinationNode.dayFlags & NK2AI::DayFlags::FLY_CAST, NK2AI::DayFlags::NONE);
	EXPECT_EQ(destinationNode.dayFlags & NK2AI::DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST, NK2AI::DayFlags::NONE);
}
