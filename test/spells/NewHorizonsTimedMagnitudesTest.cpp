/* NewHorizonsTimedMagnitudesTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsTimedMagnitudes.h"
TEST(TimedMagnitudes, FrailtyPerkFollowsBaseCapAndHydraKeepsFixedCapacity)
{
	EXPECT_EQ(newHorizonsMagic::frailtyDefenseLoss(10000,10000,0,0,0,true),2500);
	EXPECT_EQ(newHorizonsMagic::frailtyDefenseLoss(0,14500,20,30,20,false),1000);
	EXPECT_EQ(newHorizonsMagic::hydraCapacityMillionths(0,14500,20,30,20),25000000);
	EXPECT_EQ(newHorizonsMagic::hydraCapacityMillionths(10000,14500,20,30,20),50000000);
	// Non-damage specialty is20%;100 is not an admitted scaler input, even at zero power.
	EXPECT_THROW(newHorizonsMagic::frailtyDefenseLoss(0,14500,20,30,100,false), std::invalid_argument);
	EXPECT_THROW(newHorizonsMagic::hydraCapacityMillionths(0,14500,20,30,100), std::invalid_argument);
}
TEST(TimedMagnitudes, DurationCapsAndChanceFloorRetainPerkOrdering)
{
	EXPECT_EQ(newHorizonsMagic::entangleBaseDuration(10000,10000,0,0,true),3);
	EXPECT_EQ(newHorizonsMagic::misfortuneBaseDuration(10000,10000,0,0),4);
	EXPECT_EQ(newHorizonsMagic::misfortuneChanceMultiplier(0,10000,0,0,true),6500);
	EXPECT_EQ(newHorizonsMagic::misfortuneChanceMultiplier(10000,10000,0,0,true),2500);
	EXPECT_EQ(newHorizonsMagic::shieldOfChaosReduction(10000,10000,0,0,true),9000);
}
TEST(TimedMagnitudes, ReflectionKeepsRecipientPowerBeforeFloorAndRetributionistAfterCap)
{
	EXPECT_EQ(newHorizonsMagic::divineRetributionCap(0,14500,20,30,25),25);
	EXPECT_EQ(newHorizonsMagic::divineRetributionCap(1,10000,0,0,25),26);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(9,25,120),2);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(1000,25,120),30);
}
