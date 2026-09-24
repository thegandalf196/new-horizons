/*
 * TaskFailureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/Actions/BattleAction.h"
#include "nullkiller2/NullkillerTest.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/MiscObjects.h"

class Nullkiller2_MovementFailure : public NullkillerTest
{
protected:
	void checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero = false);

	CGHeroInstance * startHero()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("MovementFailure")
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(27), 1}});
		startWithMap(std::move(builder));
		revealMap(PlayerColor(0));
		return findHeroByOwner(PlayerColor(0));
	}
};

TEST_F(Nullkiller2_MovementFailure, unreachableDestinationFailsWithoutMoving)
{
	auto * hero = startHero();
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	const auto movement = hero->movementPointsRemaining();
	const auto target = position + int3(1, 0, 0);
	map()->getTile(target).terrainType = ETerrainId::ROCK;
	auto gateway = makeGateway(PlayerColor(0));
	CGPath path;
	ASSERT_FALSE(gateway->nullkiller->getPathsInfo(hero)->getPath(path, target));
	EXPECT_THROW(gateway->moveHeroToTile(target, NK2AI::HeroPtr(hero, gateway->cc.get())),
		NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
}

TEST_F(Nullkiller2_MovementFailure, validFutureDayDestinationIsPendingNotFailure)
{
	auto * hero = startHero();
	ASSERT_NE(hero, nullptr);
	hero->setMovementPoints(0);
	const auto position = hero->visitablePos();
	const auto target = position + int3(1, 0, 0);
	auto gateway = makeGateway(PlayerColor(0));
	CGPath path;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(path, target));
	ASSERT_FALSE(path.nodes.empty());
	EXPECT_GT(path.nodes.front().turns, 0);
	EXPECT_FALSE(gateway->moveHeroToTile(target, NK2AI::HeroPtr(hero, gateway->cc.get())));
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
}

void Nullkiller2_MovementFailure::checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("RequiredBattleRoute")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(27), 100}});
	if(useEnemyHero)
		builder.playerActive(PlayerColor(1))
			.hero({10, 5, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
	else if(!useGarrison)
		builder.monster({10, 5, 0}, CreatureID(0), 1);
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	if(useGarrison)
	{
		auto garrison = std::make_shared<CGGarrison>(gameState().get());
		garrison->id = ObjectInstanceID(static_cast<int>(map()->objects.size()));
		garrison->ID = Obj::GARRISON;
		garrison->subID = MapObjectSubID(0);
		garrison->tempOwner = PlayerColor::NEUTRAL;
		garrison->removableUnits = true;
		// Reuse visit-direction geometry only; this test does not render art.
		garrison->appearance = hero->appearance;
		const int3 guardPosition(10, 5, 0);
		garrison->pos = guardPosition + garrison->getVisitableOffset();
		ASSERT_TRUE(garrison->setCreature(SlotID(0), CreatureID(0), 1));
		map()->objects.push_back(garrison);
		map()->getTile(guardPosition).blockingObjects.push_back(garrison->id);
		map()->getTile(guardPosition).visitableObjects.push_back(garrison->id);
	}
	hero->setMovementPoints(20);
	auto gateway = makeGateway(PlayerColor(0));
	const int3 target(15, 5, 0);
	CGPath livePath;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(livePath, {6, 5, 0}));
	ASSERT_FALSE(gateway->nullkiller->getPathsInfo(hero)->getPath(livePath, target));
	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(hero, NK2AI::MAIN);
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = false;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(paths.empty());
	for(const auto & path : paths)
	{
		EXPECT_TRUE(std::ranges::any_of(path.nodes, [](const NK2AI::AIPathNodeInfo & node)
		{
			return dynamic_cast<const NK2AI::AIPathfinding::BattleAction *>(node.specialAction.get()) != nullptr;
		}));
	}
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(false);
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredGarrisonBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(true);
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredEnemyHeroBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(false, true);
}

TEST(Nullkiller2_Engine_TaskFailure, triesNextTaskWhenAnotherCandidateIsAvailable)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, true, false),
		NK2AI::TaskFailureAction::TRY_NEXT_TASK);
}

TEST(Nullkiller2_Engine_TaskFailure, replansAfterPreviousProgressEvenWithRemainingTasks)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(true, true, false),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, replansAfterPreviousProgress)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(true, false, false),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, replansWhenAnotherHeroCanStillMove)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, false, true),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, stopsWhenNoProgressOrAlternativeExists)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, false, false),
		NK2AI::TaskFailureAction::STOP_TURN);
}
