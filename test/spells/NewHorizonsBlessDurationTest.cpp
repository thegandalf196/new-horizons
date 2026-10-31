/* NewHorizonsBlessDurationTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsBlessDuration.h"
#include "../../lib/spells/NewHorizonsTimedUtility.h"
TEST(BlessDuration, PowerTermFloorsBeforeTheOrdinaryDurationCap)
{
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(79, 10000, 0, 0, 0), 2);
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(80, 10000, 0, 0, 0), 3);
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(160, 10000, 0, 0, 0), 4);
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(1000000, 10000, 20, 30, 20), 4);
	EXPECT_THROW(newHorizonsMagic::blessOrdinaryDuration(1000000, 10000, 20, 30, 100), std::invalid_argument);
}
TEST(BlessDuration, CastComponentModifiersAffectOnlyThePowerTerm)
{
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(40, 10000, 0, 0, 0), 2);
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(40, 10000, 100, 0, 0), 3);
	EXPECT_EQ(newHorizonsMagic::blessOrdinaryDuration(40, 10000, 0, 100, 0), 3);
	EXPECT_EQ(newHorizonsMagic::durationWithCastExtensions(4, true, true), 6);
}
