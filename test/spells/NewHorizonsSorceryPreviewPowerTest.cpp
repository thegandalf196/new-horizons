/*
 * NewHorizonsSorceryPreviewPowerTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"

TEST(NewHorizonsSorceryPreviewPowerTest, ChronomancerStrengthensOnceBeforeMassNormalizationAndCapsAtSixty)
{
	using newHorizonsSorceryRevision::finalSlowValue;
	EXPECT_DOUBLE_EQ(finalSlowValue(-20, 100, true), -24);
	EXPECT_DOUBLE_EQ(finalSlowValue(-31, 100, true), -37.2);
	EXPECT_DOUBLE_EQ(finalSlowValue(-50, 100, true), -60);
	EXPECT_DOUBLE_EQ(finalSlowValue(-75, 100, true), -60);
	EXPECT_DOUBLE_EQ(finalSlowValue(-20, 60, true), -14);
	EXPECT_DOUBLE_EQ(finalSlowValue(-31, 60, true), -22);
	EXPECT_DOUBLE_EQ(finalSlowValue(-50, 60, true), -36);
	EXPECT_DOUBLE_EQ(finalSlowValue(-75, 60, true), -36);
	EXPECT_DOUBLE_EQ(finalSlowValue(-50, 60, false), -30);
	EXPECT_DOUBLE_EQ(finalSlowValue(7, 60, true), 7);
}

TEST(NewHorizonsSorceryPreviewPowerTest, LivingArmorKeepsLoadedBaseOutsideThePowerFloor)
{
	using newHorizonsSorceryRevision::livingArmorHitPoints;
	EXPECT_EQ(livingArmorHitPoints(40, 1, 0, 10, 12650, 25), 40);
	EXPECT_EQ(livingArmorHitPoints(40, 1, 99, 10, 10000, 0), 49);
	EXPECT_EQ(livingArmorHitPoints(40, 1, 100, 10, 12650, 25), 55);
	EXPECT_EQ(livingArmorHitPoints(80, 1, 100, 10, 12650, 25), 95);
	EXPECT_EQ(livingArmorHitPoints(0, 1, 100, 10, 12650, 25), 0);
	EXPECT_EQ(livingArmorHitPoints(40, -1, 100, 10, 12650, 25), 0);
	EXPECT_EQ(livingArmorHitPoints(40, 1, -100, 10, 10000, 0), 40);
}
