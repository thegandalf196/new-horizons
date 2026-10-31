/*
 * NewHorizonsSorceryPreviewPowerTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"

TEST(NewHorizonsSorceryPreviewPowerTest, FocusSharesCapturedProfileAndOnePowerFloor)
{
	using newHorizonsSorceryRevision::focusMarkBasisPoints;
	const newHorizonsSorceryRevision::FocusRules revised{500, 3, 1200};
	const newHorizonsSorceryRevision::FocusRules legacy{1000, 5, 2000};
	EXPECT_EQ(focusMarkBasisPoints(revised, 0, 12650, 10, 25, 20), 500);
	EXPECT_EQ(focusMarkBasisPoints(revised, 100, 10000, 0, 0, 0), 800);
	EXPECT_EQ(focusMarkBasisPoints(revised, 100, 12650, 10, 25, 20), 1126);
	EXPECT_EQ(focusMarkBasisPoints(revised, 200, 12650, 10, 25, 20), 1200);
	EXPECT_EQ(focusMarkBasisPoints(legacy, 100, 10000, 0, 0, 0), 1500);
	EXPECT_THROW(focusMarkBasisPoints(revised, -1, 10000, 0, 0, 0), std::invalid_argument);
}

TEST(NewHorizonsSorceryPreviewPowerTest, LivingArmorKeepsLoadedBaseOutsideThePowerFloor)
{
	using newHorizonsSorceryRevision::livingArmorHitPoints;
	EXPECT_EQ(livingArmorHitPoints(40, 1, 0, 10, 12650, 10, 25), 40);
	EXPECT_EQ(livingArmorHitPoints(40, 1, 99, 10, 10000, 0, 0), 49);
	EXPECT_EQ(livingArmorHitPoints(40, 1, 100, 10, 12650, 10, 25), 57);
	EXPECT_EQ(livingArmorHitPoints(80, 1, 100, 10, 12650, 10, 25), 97);
	EXPECT_EQ(livingArmorHitPoints(0, 1, 100, 10, 12650, 10, 25), 0);
	EXPECT_EQ(livingArmorHitPoints(40, -1, 100, 10, 12650, 10, 25), 0);
	EXPECT_EQ(livingArmorHitPoints(40, 1, -100, 10, 10000, 0, 0), 40);
}
