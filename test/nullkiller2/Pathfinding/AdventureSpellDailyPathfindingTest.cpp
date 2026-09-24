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
#include "SpellPointTestUtils.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/GameLibrary.h"
#include "lib/GameConstants.h"
#include "lib/IGameSettings.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/pathfinder/PathfinderOptions.h"
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

class AdventureSpellDailyPathfindingTest : public NullkillerTest
{
protected:
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

	CGHeroInstance * startHero(bool newHorizons)
	{
		useNewHorizonsRules = newHorizons;
		const SpellID fly = spell("core:fly");
		const SpellID waterWalk = spell("core:waterWalk");
		const SpellID summonBoat = spell("core:summonBoat");
		const SpellID dimensionDoor = spell("core:dimensionDoor");
		const SpellID townPortal = spell("core:townPortal");

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.name("AdventureSpellDailyPathfinding")
			.playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.heroPrimary(10, 10, 10, 20)
			.heroSpells({fly, waterWalk, summonBoat, dimensionDoor, townPortal})
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
