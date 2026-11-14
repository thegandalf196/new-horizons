/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include <gtest/gtest.h>
#include "../widgets/LeadershipEfficiencyBadge.h"
#include <limits>

TEST(LeadershipEfficiencyBadgeTest, FullAndInvalidEfficiencyHaveNoWarning)
{
	using namespace leadershipPresentation;
	EXPECT_FALSE(reducedEfficiency(1.0));
	EXPECT_FALSE(reducedEfficiency(1.1));
	EXPECT_FALSE(reducedEfficiency(-0.1));
	EXPECT_FALSE(reducedEfficiency(std::numeric_limits<double>::quiet_NaN()));
	EXPECT_FALSE(reducedEfficiency(std::numeric_limits<double>::infinity()));
	EXPECT_EQ(compactPercent(1.0), 100);
	EXPECT_EQ(compactPercent(-std::numeric_limits<double>::max()), 0);
}

TEST(LeadershipEfficiencyBadgeTest, ReducedEfficiencyNeverRoundsBackToOneHundred)
{
	using namespace leadershipPresentation;
	const auto barelyReduced = std::nextafter(1.0, 0.0);
	EXPECT_TRUE(reducedEfficiency(barelyReduced));
	EXPECT_EQ(compactPercent(barelyReduced), 99);
	EXPECT_EQ(compactPercent(0.789), 78);
	EXPECT_EQ(compactPercent(0.009), 0);
	EXPECT_TRUE(reducedEfficiency(0.009));
}

TEST(LeadershipEfficiencyBadgeTest, NativeMiniatureAndBattleBadgesStayCompact)
{
	using namespace leadershipPresentation;
	const auto battle = badgeSize(BadgeRole::BATTLE);
	const auto portrait = badgeSize(BadgeRole::PORTRAIT);
	const auto miniature = badgeSize(BadgeRole::MINIATURE);
	EXPECT_EQ(battle.width, 23);
	EXPECT_EQ(battle.height, 9);
	EXPECT_LT(battle.width, 30);
	EXPECT_LT(battle.height, 11);
	EXPECT_EQ(battle.width, miniature.width);
	EXPECT_EQ(battle.height, miniature.height);
	EXPECT_LT(battle.width, portrait.width);
	EXPECT_LT(battle.height, portrait.height);
	EXPECT_LT(miniature.width, portrait.width);
	EXPECT_LT(miniature.height, portrait.height);
	EXPECT_EQ(portrait.width, 34);
	EXPECT_EQ(portrait.height, 11);
}
