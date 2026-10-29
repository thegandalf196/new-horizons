/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsBloodAerialFixture.h"
TEST_F(NewHorizonsBloodAerialFixture, SurvivingAerialCounterResolvesBloodAfterShotCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepareBoundary(false));
	ASSERT_TRUE(shootBoundary());
	EXPECT_TRUE(griffin->alive());
	EXPECT_TRUE(basilisk->alive());
	EXPECT_EQ(basilisk->petrifyingBloodRound, battle()->getRound());
	EXPECT_TRUE(petrified(*griffin));
}
TEST_F(NewHorizonsBloodAerialFixture, LaterShotDeathPreventsEarlierAerialBloodAttempt)
{
	ASSERT_NO_FATAL_FAILURE(prepareBoundary(true));
	ASSERT_TRUE(shootBoundary());
	EXPECT_FALSE(griffin->alive());
	EXPECT_TRUE(basilisk->alive());
	EXPECT_EQ(basilisk->petrifyingBloodRound, battle()->getRound());
	EXPECT_FALSE(petrified(*griffin));
}
