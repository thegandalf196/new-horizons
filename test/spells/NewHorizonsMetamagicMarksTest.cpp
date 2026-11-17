/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/SpellEffectScalingProfile.h"
#include "../../lib/spells/NewHorizonsTimedMagnitudes.h"
#include "../../lib/spells/NewHorizonsMetamagicTimedStatus.h"
#include "../../lib/spells/NewHorizonsDivineRetribution.h"
#include "../../lib/spells/NewHorizonsHandOfFateTargeting.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include <limits>
namespace
{
using namespace newHorizonsMetamagic;
Profile capturedWisdom()
{
	Profile value;
	value.wisdomEffectivenessBonus = 75;
	return value;
}
}

TEST(OrdinarySpellScaling, HandOfFateWeightsPreserveUniformAndFateDealerDistributions)
{
	using namespace newHorizonsHandOfFate;
	EXPECT_EQ(selectionDenominator(4, false), 4);
	EXPECT_EQ(selectionWeight(4, 1, true, false), 1);
	EXPECT_EQ(selectionWeight(4, 1, false, false), 1);
	EXPECT_EQ(selectionDenominator(4, true), 16);
	EXPECT_EQ(selectionWeight(4, 1, true, true), 7);
	EXPECT_EQ(selectionWeight(4, 1, false, true), 3);
	EXPECT_EQ(selectionWeight(4, 4, true, true), 4);
	EXPECT_EQ(selectionWeight(4, 0, false, true), 4);
}

TEST(OrdinarySpellScaling, HandOfFateExpectedFractionsIncludeBlockedCollateral)
{
	using newHorizonsHandOfFate::expectedManaFraction;
	const auto fraction = [](uint32_t numerator, uint32_t denominator)
	{ return std::pair<uint32_t, uint32_t>{numerator, denominator}; };
	EXPECT_EQ(expectedManaFraction(false, 4, 0, 0), fraction(0, 8));
	EXPECT_EQ(expectedManaFraction(true, 4, 0, 0), fraction(8, 8));
	EXPECT_EQ(expectedManaFraction(true, 4, 4, 0), fraction(4, 8));
	EXPECT_EQ(expectedManaFraction(true, 4, 4, 4), fraction(8, 8));
	EXPECT_EQ(expectedManaFraction(true, 4, 2, 1), fraction(7, 8));
	EXPECT_EQ(expectedManaFraction(false, 4, 2, 1), fraction(1, 8));
	EXPECT_EQ(expectedManaFraction(true, 0, 0, 0), fraction(1, 1));
	EXPECT_THROW(expectedManaFraction(true, 4, 2, 3), std::invalid_argument);
}

TEST(OrdinarySpellScaling, NativeDamageCapFollowsCapturedOutputScaling)
{
	const auto value = capturedWisdom();
	EXPECT_EQ(spellDamageOutput(5, value, 5, 20), 5);
	EXPECT_EQ(spellDamageOutput(5, value, 5), 5);
	EXPECT_EQ(spellDamageOutput(5, value, 0, 20), 9);
	EXPECT_EQ(spellDamageOutput(150, Profile{}, 0, 20), 180);
	EXPECT_EQ(spellDamageOutput(5, Profile{}, 0), 5);
}

TEST(OrdinarySpellScaling, FrailtyScalesEachNewFractionAndNeverReamplifiesItsCumulativeHistory)
{
	const auto value = capturedWisdom();
	const auto castFraction = scaleMagnitude(2000, value);
	EXPECT_EQ(castFraction, 3500);
	const auto first = newHorizonsMagic::frailtyAccumulation(100, 0, castFraction);
	EXPECT_EQ(first.totalBasisPoints, 3500);
	EXPECT_EQ(first.defenseLoss, 35);
	const auto second = newHorizonsMagic::frailtyAccumulation(100, first.totalBasisPoints, castFraction);
	EXPECT_EQ(second.totalBasisPoints, 6000);
	EXPECT_EQ(second.defenseLoss, 60);
	const auto third = newHorizonsMagic::frailtyAccumulation(100, second.totalBasisPoints, castFraction);
	EXPECT_EQ(third.totalBasisPoints, 6000);
	EXPECT_EQ(third.defenseLoss, 60);
	EXPECT_EQ(newHorizonsMagic::frailtyAccumulation(7, 0, castFraction).defenseLoss, 2);
}

TEST(OrdinarySpellScaling, DivineRetributionSnapshotsRatioAndCapBeforeFutureWisdomChanges)
{
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(200, 150, 100), 60);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(200, 262, 100, 5250), 105);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(1000, 262, 100, 5250), 262);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(1000, 262, 120, 5250), 314);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(7, 262, 100, 5250), 3);
	EXPECT_THROW(newHorizonsMagic::divineRetributionPayout(200, 262, 100, 6901), std::invalid_argument);
}

TEST(OrdinarySpellScaling, DivineRetributionDistinguishesHistoricalAbsenceFromCurrentRatioPresence)
{
	Bonus marker;
	EXPECT_FALSE(newHorizonsDivineRetribution::capturedPayoutRatioBasisPoints(marker));
	EXPECT_EQ(newHorizonsDivineRetribution::payoutRatioBasisPoints(marker), 3000);
	JsonNode saved;
	saved["payoutRatioBasisPoints"].Integer() = 3000;
	marker.parameters = std::make_shared<BonusParameters>(saved);
	EXPECT_EQ(newHorizonsDivineRetribution::capturedPayoutRatioBasisPoints(marker), 3000);
	saved["payoutRatioBasisPoints"].Integer() = 5250;
	marker.parameters = std::make_shared<BonusParameters>(saved);
	EXPECT_EQ(newHorizonsDivineRetribution::payoutRatioBasisPoints(marker), 5250);
	for(const int value : {2999, 6901})
	{
		saved["payoutRatioBasisPoints"].Integer() = value;
		marker.parameters = std::make_shared<BonusParameters>(saved);
		EXPECT_THROW(newHorizonsDivineRetribution::payoutRatioBasisPoints(marker), std::runtime_error);
	}
	saved["payoutRatioBasisPoints"] = JsonNode{};
	marker.parameters = std::make_shared<BonusParameters>(saved);
	EXPECT_THROW(newHorizonsDivineRetribution::payoutRatioBasisPoints(marker), std::runtime_error);
	saved["payoutRatioBasisPoints"].Float() = 5250.5;
	marker.parameters = std::make_shared<BonusParameters>(saved);
	EXPECT_THROW(newHorizonsDivineRetribution::payoutRatioBasisPoints(marker), std::runtime_error);
}

TEST(OrdinarySpellScaling, ChaosShieldAmplifiesOnlyItsNumericalReductionUnderTheNativeCap)
{
	const auto profile = capturedWisdom();
	EXPECT_EQ(chaosShieldMagnitude(5000, Profile{}, 8000), 5000);
	EXPECT_EQ(chaosShieldMagnitude(5000, profile, 8000), 8000);
	EXPECT_EQ(chaosShieldMagnitude(6000, profile, 9000), 9000);
}

TEST(OrdinarySpellScaling, MisfortuneAmplifiesReductionFromNeutralAndRetainsItsNativeFloor)
{
	const auto profile = capturedWisdom();
	EXPECT_EQ(misfortuneChanceFactor(6500, Profile{}), 6500);
	EXPECT_EQ(misfortuneChanceFactor(6500, profile), 3875);
	EXPECT_EQ(misfortuneChanceFactor(5000, profile), 2500);
	EXPECT_EQ(misfortuneChanceFactor(2500, profile), 2500);
}

TEST(OrdinarySpellScaling, CapturedWisdomBoundsFloorsAndSaturationRemainChecked)
{
	Profile value;
	value.wisdomEffectivenessBonus = 50;
	EXPECT_EQ(scaleMagnitude(83, value), 124);
	EXPECT_EQ(scaleSignedBonus(-3, value), -4);
	EXPECT_EQ(scaleDuration(3, value), 4);
	EXPECT_EQ(scaleDuration(0, value), 0);
	EXPECT_EQ(scaleDuration(-1, value), -1);
	EXPECT_EQ(scaleMagnitude(std::numeric_limits<int64_t>::max(), value), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(scaleSignedBonus(std::numeric_limits<int32_t>::min(), value), std::numeric_limits<int32_t>::min());
	EXPECT_THROW(scaleMagnitude(-1, value), std::invalid_argument);
	for(const int invalid : {-1, 1001})
	{
		value.wisdomEffectivenessBonus = invalid;
		EXPECT_THROW(scaleMagnitude(1, value), std::invalid_argument);
	}
}
