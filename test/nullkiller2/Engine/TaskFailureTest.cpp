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
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Goals/Composition.h"
#include "AI/Nullkiller2/Goals/Invalid.h"
#include "nullkiller2/NullkillerTest.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/MiscObjects.h"
#include "lib/CPlayerState.h"

class Nullkiller2_MovementFailure : public NullkillerTest
{
protected:
	void checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero = false, bool useTownPurchase = false);

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

void Nullkiller2_MovementFailure::checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero, bool useTownPurchase)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("RequiredBattleRoute")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison(useTownPurchase
			? std::vector<std::pair<CreatureID, uint16_t>>{{CreatureID(0), 1}}
			: std::vector<std::pair<CreatureID, uint16_t>>{{CreatureID(27), 100}});
	if(useTownPurchase)
		builder.town({6, 3, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({});
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
			map()->getTile({x, y, 0}).terrainType = y == 5 || (useTownPurchase && x < 9)
				? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	if(useTownPurchase)
	{
		auto * town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		town->addBuilding(BuildingID::DWELL_LVL_1);
		town->creatures.at(0) = {40, {CreatureID(0)}};
		grantResources(PlayerColor(0), GameResID(GameResID::GOLD), 100000);
	}
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
	settings.useHeroChain = useTownPurchase;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(paths.empty());
	if(useTownPurchase)
	{
		ASSERT_TRUE(std::ranges::any_of(paths, [](const NK2AI::AIPath & path)
		{
			return path.exchangeCount > 1;
		}));
	}
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

TEST_F(Nullkiller2_MovementFailure, townPurchaseRoutePreservesRequiredEnemyHeroBattle)
{
	checkRequiredBattleRoute(false, true, true);
}

TEST_F(Nullkiller2_MovementFailure, townPurchaseRoutePreservesRequiredGarrisonBattle)
{
	checkRequiredBattleRoute(true, false, true);
}

TEST_F(Nullkiller2_MovementFailure, alliedHeroCanBlockPreviouslyPlannedCorridor)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("AlliedCorridorBlocker")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.hero({10, 8, 0}, HeroTypeID(1), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}});
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 || y == 8
				? ETerrainId::GRASS : ETerrainId::ROCK;
	auto gateway = makeGateway(PlayerColor(0));
	const CGHeroInstance * traveler = nullptr;
	const CGHeroInstance * blocker = nullptr;
	for(const auto * hero : gateway->cc->getHeroesInfo())
	{
		if(hero->visitablePos().y == 5)
			traveler = hero;
		else
			blocker = hero;
	}
	ASSERT_NE(traveler, nullptr);
	ASSERT_NE(blocker, nullptr);
	const int3 target(15, 5, 0);
	CGPath live;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(traveler, NK2AI::MAIN);
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = false;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	const auto retainedPlans = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(retainedPlans.empty());
	const NK2AI::Goals::ExecuteHeroChain queuedRoute(retainedPlans.front());
	NK2AI::Goals::Composition queuedComposition;
	queuedComposition.addNext(queuedRoute);
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	EXPECT_EQ(queuedComposition.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	const auto original = blocker->pos;
	map()->moveObject(blocker->id, int3(10, 5, 0) + blocker->getVisitableOffset());
	gateway->nullkiller->invalidatePaths();
	EXPECT_FALSE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), traveler);
	EXPECT_EQ(queuedComposition.getBlockedInitialRoute(gateway->nullkiller.get()), traveler);
	EXPECT_FALSE(gateway->nullkiller->isHeroLocked(traveler));
	// An earlier task or special action must execute before a later route can
	// be judged; it may remove the obstruction or transport the hero.
	NK2AI::Goals::Composition earlierTask;
	earlierTask.addNextSequence({NK2AI::Goals::sptr(NK2AI::Goals::Invalid()), NK2AI::Goals::sptr(queuedRoute)});
	EXPECT_EQ(earlierTask.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	auto specialPath = retainedPlans.front();
	specialPath.nodes.back().specialAction = std::make_shared<NK2AI::AIPathfinding::BattleAction>(target);
	EXPECT_EQ(NK2AI::Goals::ExecuteHeroChain(specialPath).getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	// A copied plan remains a historical plan, not proof that its route is still open.
	EXPECT_EQ(retainedPlans.front().targetTile(), target);
	map()->moveObject(blocker->id, original);
	gateway->nullkiller->invalidatePaths();
	EXPECT_TRUE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
}

TEST_F(Nullkiller2_MovementFailure, armyChangeRefreshesGuardedRoutesWithoutMovingHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("ArmyChangeRoute")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.monster({10, 5, 0}, CreatureID(0), 100);
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	auto gateway = makeGateway(PlayerColor(0));
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const int3 target(15, 5, 0);
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(27), 100));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const auto strongerPaths = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(strongerPaths.empty());
	for(const auto & path : strongerPaths)
	{
		EXPECT_TRUE(std::ranges::any_of(path.nodes, [](const NK2AI::AIPathNodeInfo & node)
		{
			return dynamic_cast<const NK2AI::AIPathfinding::BattleAction *>(node.specialAction.get()) != nullptr;
		}));
	}
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(0), 1));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 3));
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	EXPECT_EQ(hero->visitablePos(), position);
}

TEST_F(Nullkiller2_MovementFailure, armyChangeRefreshesEnemyRoutesThroughStationaryHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("ArmyChangeThreat")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({10, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.hero({5, 5, 0}, HeroTypeID(1), PlayerColor(1))
		.heroGarrison({{CreatureID(0), 100}});
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	auto gateway = makeGateway(PlayerColor(0));
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const int3 target(15, 5, 0);
	EXPECT_GT(gateway->nullkiller->dangerHitMap->getTileThreat(target).maximumDanger.danger, 0);
	const auto * enemy = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(enemy, nullptr);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, hero), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, enemy), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), enemy), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), hero), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(hero), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy), 0);
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(27), 100));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	EXPECT_FALSE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_FALSE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	EXPECT_EQ(gateway->nullkiller->dangerHitMap->getTileThreat(target).maximumDanger.danger, 0);
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	EXPECT_EQ(hero->visitablePos(), position);
	CGHeroInstance neutralVisitor(gameState().get());
	neutralVisitor.setOwner(PlayerColor::NEUTRAL);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, &neutralVisitor), 0);
	gameState()->getPlayerTeam(PlayerColor(1))->players.erase(PlayerColor(1));
	gameState()->getPlayerTeam(PlayerColor(0))->players.insert(PlayerColor(1));
	gameState()->getPlayerState(PlayerColor(1))->team = gameState()->getPlayerState(PlayerColor(0))->team;
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, enemy), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), hero), 0);
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
