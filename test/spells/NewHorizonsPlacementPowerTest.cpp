/* NewHorizonsPlacementPowerTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsPlacementPower.h"
#include "../../lib/json/JsonNode.h"
TEST(PlacementPower, WholePoolPerksRetainSeparateFloorsAndActualFootprint)
{
	EXPECT_EQ(newHorizonsMagic::verdantPrisonHitPoints(1,10000,0,0,true),228);
	EXPECT_EQ(newHorizonsMagic::transfigureHitPoints(1,10000,0,0,1,true),165);
	EXPECT_EQ(newHorizonsMagic::transfigureHitPoints(1,10000,0,0,2,true),227);
}
TEST(PlacementPower, EarthquakeUsesLoadedProfileAndSeparateSectionCoefficient)
{
	JsonNode profile;
	profile["baseSections"].Integer()=2;profile["maxSections"].Integer()=4;profile["powerPerSection"].Integer()=80;
	profile["baseDamage"].Integer()=30;profile["powerNumerator"].Integer()=4;profile["powerDivisor"].Integer()=5;
	EXPECT_EQ(newHorizonsMagic::earthquakeSectionCount(profile,79,10000,0,0),2);
	EXPECT_EQ(newHorizonsMagic::earthquakeSectionCount(profile,80,10000,0,0),3);
	EXPECT_EQ(newHorizonsMagic::earthquakeRawDamage(profile,1,10000,0,0),30);
	EXPECT_EQ(newHorizonsMagic::earthquakeStructuralDamage(101,true),126);
}
