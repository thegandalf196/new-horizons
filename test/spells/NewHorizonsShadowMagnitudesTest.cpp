/*
 * NewHorizonsShadowMagnitudesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "../../lib/entities/hero/NewHorizonsSchoolSpellcraft.h"

#include <limits>

TEST(NewHorizonsShadowMagnitudesTest, VampirismScalesOnlyPowerWithOneFinalFloor)
{
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(0, 10000, 0, 0, false), 2500);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(100, 10000, 0, 0, false), 4000);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(100, 11500, 0, 0, false), 4225);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(20, 12650, 0, 0, false), 2879);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(20, 12650, 20, 25, false), 3069);
}

TEST(NewHorizonsShadowMagnitudesTest, NightFeederAppliesAfterTheOrdinaryCap)
{
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(0, 100000, 1000, 1000, true), 4000);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(100, 14300, 0, 0, true), 6145);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(166, 10000, 0, 0, false), 4990);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(167, 10000, 0, 0, false), 5000);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(166, 10000, 0, 0, true), 6490);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(167, 10000, 0, 0, true), 6500);
}

TEST(NewHorizonsShadowMagnitudesTest, DoomPreservesFixedThirtyFiveAndCapSixty)
{
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(0, 100000, 1000, 1000), 35);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(100, 10000, 0, 0), 50);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(100, 11500, 0, 0), 52);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(100, 14500, 0, 0), 56);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(166, 10000, 0, 0), 59);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(167, 10000, 0, 0), 60);
}

TEST(NewHorizonsShadowMagnitudesTest, ExplicitCastCoefficientAndBonusesAreNotRecomputed)
{
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(50, 11500, 20, 25), 47);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(114, 14599, 0, 0), 59);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(115, 14599, 0, 0), 60);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(20, 14599, 20, 25, false), 3156);
}

TEST(NewHorizonsShadowMagnitudesTest, NegativeAndZeroPowerKeepTheirExistingFamilySemantics)
{
	EXPECT_THROW(newHorizonsMagic::vampirismHealBasisPoints(-1, 10000, 0, 0, false), std::invalid_argument);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(-1, 10000, 0, 0), 35);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(100, 0, 1000, 1000, false), 2500);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(100, 0, 1000, 1000), 35);
}

TEST(NewHorizonsShadowMagnitudesTest, LargePowerCapsBeforeNarrowing)
{
	const auto power = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(power, 100000, 1000, 1000, false), 5000);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(power, 100000, 1000, 1000, true), 6500);
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(power, 100000, 1000, 1000), 60);
}

TEST(NewHorizonsShadowMagnitudesTest, InvalidCoefficientAndCastBonusDomainsRemainRejected)
{
	for(const int32_t coefficient : {-1, 100001})
	{
		EXPECT_THROW(newHorizonsMagic::vampirismHealBasisPoints(100, coefficient, 0, 0, false), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::doomCripplingPenaltyPercent(100, coefficient, 0, 0), std::invalid_argument);
	}
	for(const int32_t bonus : {-1, 1001})
	{
		EXPECT_THROW(newHorizonsMagic::vampirismHealBasisPoints(100, 10000, bonus, 0, false), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::vampirismHealBasisPoints(100, 10000, 0, bonus, false), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::doomCripplingPenaltyPercent(100, 10000, bonus, 0), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::doomCripplingPenaltyPercent(100, 10000, 0, bonus), std::invalid_argument);
	}
}

TEST(NewHorizonsShadowMagnitudesTest, SoulChainUsesCapturedCoefficientAndBinderAfterTheBaseCap)
{
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(0, 10000, false), 2000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(0, 10000, true), 3500);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(100, 10000, false), 3000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(100, 11500, false), 3150);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(200, 10000, false), 4000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(200, 10000, true), 5500);
}

TEST(NewHorizonsShadowMagnitudesTest, SoulChainFractionalCoefficientFloorsBeforeCapping)
{
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1, 9999, false), 2009);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1, 14599, false), 2014);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(136, 14599, false), 3985);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(137, 14599, false), 4000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(136, 14599, true), 5485);
}

TEST(NewHorizonsShadowMagnitudesTest, SoulChainExistingDomainAndLargePowerRemainUnchanged)
{
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(100, 0, true), 3500);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(std::numeric_limits<int32_t>::max(), 100000, true), 5500);
	EXPECT_THROW(newHorizonsSoulChain::echoPercentBasisPoints(-1, 10000, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSoulChain::echoPercentBasisPoints(0, -1, false), std::invalid_argument);
	EXPECT_THROW(newHorizonsSoulChain::echoPercentBasisPoints(0, 100001, false), std::invalid_argument);
}

TEST(NewHorizonsShadowMagnitudesTest, SoulChainAcceptsTheMaximumCanonicalCapturedCoefficient)
{
	newHorizonsHeroes::SpellcraftSpecialtyFactors factors;
	factors.schoolPercent = 145;
	factors.ordinaryEfficiencyPercent = 130;
	factors.enhancedEfficiencyBasisPoints = newHorizonsHeroes::strengthenedSpellcraftBasisPoints(130, 200);
	factors.scope = newHorizonsHeroes::SpellcraftSpecialtyScope::NUMERICAL;
	const auto coefficient = newHorizonsHeroes::composeSpellcraftCoefficient(
		factors, newHorizonsHeroes::SpellcraftComponent::NUMERICAL, 200);
	ASSERT_EQ(coefficient, 82650);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1, coefficient, false), 2082);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1, coefficient, true), 3582);
}

TEST(NewHorizonsShadowMagnitudesTest, LifeDrainWithoutHeroUsesOrdinaryHealingRate)
{
	EXPECT_EQ(newHorizonsMagic::lifeDrainHealingPercent(nullptr), 60);
}
