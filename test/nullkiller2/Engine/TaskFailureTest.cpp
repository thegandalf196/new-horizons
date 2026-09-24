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
#include "nullkiller2/NullkillerTest.h"
#include "lib/mapObjects/CGHeroInstance.h"

class Nullkiller2_MovementFailure : public NullkillerTest
{
protected:
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
