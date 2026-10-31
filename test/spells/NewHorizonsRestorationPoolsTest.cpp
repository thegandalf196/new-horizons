/* NewHorizonsRestorationPoolsTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsRestorationPools.h"
TEST(RestorationPools, FixedResurrectionPoolStaysOutsideAllPowerModifiers)
{
	EXPECT_EQ(newHorizonsMagic::resurrectionRestorationPool(0, 14500, 20, 30, 20, 25), 100);
	EXPECT_EQ(newHorizonsMagic::resurrectionRestorationPool(-3, 14500, 20, 30, 20), 100);
	EXPECT_EQ(newHorizonsMagic::resurrectionRestorationPool(1, 10000, 0, 0, 0), 104);
	EXPECT_THROW(newHorizonsMagic::resurrectionRestorationPool(0, 14500, 20, 30, 100, 25), std::invalid_argument);
}
TEST(RestorationPools, GuardianRetainsHealerThenWholePoolGuardianFloors)
{
	EXPECT_EQ(newHorizonsMagic::guardianSpiritHitPoints(1, 10000, 0, 0, 0, true, true), 65);
	EXPECT_EQ(newHorizonsMagic::guardianSpiritHitPoints(3, 10000, 0, 0, 0, true, true, 25), 72);
	EXPECT_EQ(newHorizonsMagic::guardianSpiritHitPoints(0, 14500, 20, 30, 20, true, true, 25), 62);
	EXPECT_THROW(newHorizonsMagic::guardianSpiritHitPoints(0, 14500, 20, 30, 100, true, true, 25), std::invalid_argument);
}
TEST(RestorationPools, RecipientPowerRatioPrecedesSharedFloor)
{
	EXPECT_EQ(newHorizonsMagic::resurrectionRestorationPool(1, 13000, 0, 0, 0, 25), 106);
	EXPECT_EQ(newHorizonsMagic::guardianSpiritHitPoints(1, 13000, 0, 0, 0, false, false, 25), 53);
	EXPECT_THROW(newHorizonsMagic::resurrectionRestorationPool(1, 10000, 0, 0, 0, -1), std::invalid_argument);
}
