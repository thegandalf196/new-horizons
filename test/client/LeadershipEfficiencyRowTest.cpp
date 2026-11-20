/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/LeadershipEfficiencyRow.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"

TEST(LeadershipEfficiencyRow, CanonExampleUsesTheDamageFormula)
{
	// Canon #efficiency-presentation: capacity 8, stack of 10 -> 1.8 extra, 9.8 total, 97.9%.
	const double efficiency = newHorizonsHeroes::squareRootLeadershipEfficiency(10, 8);
	const auto row = leadershipEfficiencyRow::describe(10, 8, efficiency);
	EXPECT_EQ(row.value, "97.9%");
	EXPECT_NE(row.help.find("{Your stack exceeds its capacity.}"), std::string::npos);
	EXPECT_NE(row.help.find("The first 8 creatures deal full damage. The 2 above capacity add another 1.8 creatures' worth of damage."), std::string::npos);
	EXPECT_NE(row.help.find("{Result:} 9.8 creatures' worth of damage. {97.9% efficiency} compares that with what all 10 would deal at full strength."), std::string::npos);
}

TEST(LeadershipEfficiencyRow, FullEfficiencyUsesWithinCapacityText)
{
	const auto row = leadershipEfficiencyRow::describe(6, 8, 1.0);
	EXPECT_EQ(row.value, "100%");
	EXPECT_NE(row.help.find("{Your stack is within capacity.}"), std::string::npos);
}

TEST(LeadershipEfficiencyRow, BelowFullNeverReadsHundredPercent)
{
	EXPECT_EQ(leadershipEfficiencyRow::percentText(0.99999), "99.9%");
	EXPECT_EQ(leadershipEfficiencyRow::percentText(0.5), "50%");
}

TEST(LeadershipEfficiencyRow, SingularWording)
{
	const auto row = leadershipEfficiencyRow::describe(2, 1, newHorizonsHeroes::squareRootLeadershipEfficiency(2, 1));
	EXPECT_NE(row.help.find("The first creature deals full damage. The 1 above capacity adds another"), std::string::npos);
}
