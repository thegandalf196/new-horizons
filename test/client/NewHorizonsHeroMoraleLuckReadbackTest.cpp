/*
 * NewHorizonsHeroMoraleLuckReadbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/widgets/NewHorizonsMoraleLuckPresentation.h"

TEST(NewHorizonsHeroMoraleLuckReadbackTest, LegacyRulesKeepTheExistingPresentationPath)
{
	EXPECT_FALSE(newHorizonsMoraleLuckPresentation::luckReadback(false, 10, false, false, false, 0));
	EXPECT_FALSE(newHorizonsMoraleLuckPresentation::moraleReadback(false, -10, false, false, false, false, 0));
}

TEST(NewHorizonsHeroMoraleLuckReadbackTest, LuckUsesTenPointRangeAndHonorsOverrides)
{
	using newHorizonsMoraleLuckPresentation::luckReadback;

	ASSERT_TRUE(luckReadback(true, 14, false, false, false, 0));
	EXPECT_EQ(luckReadback(true, 14, false, false, false, 0)->value, 10);
	EXPECT_EQ(luckReadback(true, -14, false, false, false, 0)->value, -10);

	const auto maximum = luckReadback(true, -7, true, true, true, 4);
	ASSERT_TRUE(maximum);
	EXPECT_EQ(maximum->value, 4);
	EXPECT_TRUE(maximum->maxLuck);
	EXPECT_FALSE(maximum->noLuck);
	EXPECT_TRUE(maximum->maximumLuckLimitPresent);
	EXPECT_EQ(maximum->maximumLuckLimit, 4);

	const auto suppressed = luckReadback(true, 7, true, false, true, 4);
	ASSERT_TRUE(suppressed);
	EXPECT_EQ(suppressed->value, 0);
	EXPECT_TRUE(suppressed->noLuck);
	EXPECT_FALSE(suppressed->maximumLuckLimitPresent);

	EXPECT_EQ(luckReadback(true, 7, false, false, true, 3)->value, 3);
	EXPECT_EQ(luckReadback(true, -7, false, false, true, 3)->value, -7);
}

TEST(NewHorizonsHeroMoraleLuckReadbackTest, MoraleUsesTenPointRangeAndHonorsFloorsAndImmunity)
{
	using newHorizonsMoraleLuckPresentation::moraleReadback;

	EXPECT_EQ(moraleReadback(true, 14, false, false, false, false, 0)->value, 10);
	EXPECT_EQ(moraleReadback(true, -14, false, false, false, false, 0)->value, -10);

	const auto floor = moraleReadback(true, -5, false, false, false, true, 0);
	ASSERT_TRUE(floor);
	EXPECT_EQ(floor->value, 0);
	EXPECT_TRUE(floor->minimumMoralePresent);
	EXPECT_EQ(floor->minimumMorale, 0);

	const auto immune = moraleReadback(true, -5, false, false, true, true, 0);
	ASSERT_TRUE(immune);
	EXPECT_EQ(immune->value, 0);
	EXPECT_TRUE(immune->moraleImmune);
	EXPECT_FALSE(immune->minimumMoralePresent);

	const auto maximum = moraleReadback(true, -5, true, true, true, true, 0);
	ASSERT_TRUE(maximum);
	EXPECT_EQ(maximum->value, 10);
	EXPECT_TRUE(maximum->maxMorale);
	EXPECT_FALSE(maximum->noMorale);
	EXPECT_FALSE(maximum->minimumMoralePresent);
}

TEST(NewHorizonsHeroMoraleLuckReadbackTest, OlderSavedContextRetainsItsActualMoraleRange)
{
	using newHorizonsMoraleLuckPresentation::moraleReadback;
	EXPECT_EQ(moraleReadback(true, 10, false, false, false, false, 0, -3, 3)->value, 3);
	EXPECT_EQ(moraleReadback(true, -10, false, false, false, false, 0, -3, 3)->value, -3);
	EXPECT_EQ(moraleReadback(true, -10, false, true, false, false, 0, -3, 3)->value, 3);
}
