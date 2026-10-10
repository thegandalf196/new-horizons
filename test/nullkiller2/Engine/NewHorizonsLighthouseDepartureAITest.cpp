/*
 * NewHorizonsLighthouseDepartureAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../game/NewHorizonsLighthouseDepartureFixture.h"
#include "../../../AI/Nullkiller2/AIGateway.h"
#include "../../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../../AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "../../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "../../../lib/callback/CCallback.h"

class NewHorizonsLighthouseDepartureAITest : public NewHorizonsLighthouseDepartureTest
{
};

TEST_F(NewHorizonsLighthouseDepartureAITest, OwnedPortForecastMatchesCoreWithoutChangingHeroOrBoat)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const auto beforePosition = hero->visitablePos();
	const auto beforeMovement = hero->movementPointsRemaining();
	PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
	const int3 target = port + int3(0, 1, 0);
	const auto * core = cache.getPathsInfo(hero)->getNode(target, EPathfindingLayer::SAIL);
	ASSERT_TRUE(core->reachable());
	ASSERT_EQ(core->turns, 0);
	auto callback = makeCallback(PlayerColor(0));
	auto gateway = std::make_unique<NK2AI::AIGateway>();
	gateway->initGameInterface(std::shared_ptr<Environment>(), callback);
	NK2AI::Goals::TGoalVec tasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(tasks, 1));
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
	const auto route = std::find_if(paths.begin(), paths.end(), [this](const NK2AI::AIPath & path)
	{
		return path.targetHero == hero && path.turn() == 0;
	});
	ASSERT_NE(route, paths.end());
	EXPECT_NEAR(route->movementCost(), core->getCost(), 0.0001f);
	EXPECT_EQ(hero->visitablePos(), beforePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), beforeMovement);
	EXPECT_FALSE(hero->inBoat());
	EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero));
}

TEST(NewHorizonsLighthouseDepartureAIStateTest, SearchDayFlagHasDistinctBucketAndExpiresOnNextDay)
{
	NK2AI::AIPathNode node;
	node.turns = 0;
	node.lighthouseDepartureTurn = 0;
	node.dayFlags = NK2AI::DayFlags::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE;
	EXPECT_NE(NK2AI::newHorizonsDailyOpportunityFlags(node.dayFlags), NK2AI::DayFlags::NONE);
	EXPECT_EQ(NK2AI::dayFlagsForTurn(&node, 0), NK2AI::DayFlags::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE);
	EXPECT_EQ(NK2AI::dayFlagsForTurn(&node, 1), NK2AI::DayFlags::NONE);
	node.reset(EPathfindingLayer::LAND, EPathAccessibility::ACCESSIBLE);
	EXPECT_EQ(node.lighthouseDepartureTurn, -1);
}
