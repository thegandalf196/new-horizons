/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"
#include "../../lib/spells/NewHorizonsTimedMagnitudes.h"
#include "../../lib/json/JsonNode.h"
#include <limits>
#include "../../lib/networkPacks/SetMetamagicPointState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMetamagicTimedStatus.h"
#include "../../lib/spells/NewHorizonsDivineRetribution.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsHandOfFateTargeting.h"
#include "../mock/mock_battle_Unit.h"


namespace
{
using namespace newHorizonsMetamagic;
Profile expert()
{
	Profile value;
	value.rank = 3;
	return value;
}
}

TEST(NewHorizonsMetamagicMarks, CapturedAbsenceKeepsLegacyAndRequiresAnExplicitIntegerRevision)
{
	JsonNode rules;
	EXPECT_FALSE(usesArcaneMarks(rules));
	rules["metamagicPolicy"]["version"].Integer() = 1;
	EXPECT_FALSE(usesArcaneMarks(rules));
	rules["metamagicPolicy"]["version"].Integer() = 2;
	EXPECT_TRUE(usesArcaneMarks(rules));
	rules["metamagicPolicy"]["version"].String() = "2";
	EXPECT_FALSE(usesArcaneMarks(rules));
}

TEST(NewHorizonsMetamagicMarks, ExistingMarksScaleThisCastAndResolvedMarksOnlyAffectTheNextCast)
{
	Profile value;
	value.rank = 1;
	EXPECT_EQ(scaleMagnitude(100, value, 0), 100);
	EXPECT_EQ(marksAfterResolvedSpell(value, 0), 1);
	EXPECT_EQ(scaleMagnitude(100, value, 1), 105);
	value.residualMagic = true;
	EXPECT_EQ(marksAfterResolvedSpell(value, 0), 2);
	EXPECT_EQ(marksAfterResolvedSpell(value, 2), 3);
	EXPECT_EQ(marksAfterResolvedSpell(value, 4), 5);
	EXPECT_EQ(marksAfterResolvedSpell(value, 5), 5);
}

TEST(NewHorizonsMetamagicMarks, RankAndPerMarkPerksAddBeforeSaturationRatherThanMultiplying)
{
	for(int rank = 1; rank <= 3; ++rank)
	{
		Profile value;
		value.rank = rank;
		EXPECT_EQ(effectivenessBonusPercent(value, 5), 25 * rank);
	}
	auto value = expert();
	value.arcaneAmplification = value.arcanePerfection = value.arcaneSaturation = true;
	EXPECT_EQ(effectivenessBonusPercent(value, 0), 0);
	EXPECT_EQ(effectivenessBonusPercent(value, 4), 88);
	EXPECT_EQ(effectivenessBonusPercent(value, 5), 130);
	EXPECT_EQ(scaleMagnitude(101, value, 5), 232);
}

TEST(NewHorizonsMetamagicMarks, InitialAndSummonedSeedingAreSeparateAndNeverAddToFour)
{
	auto value = expert();
	value.perfectImprint = true;
	EXPECT_EQ(initialMarks(value), 2);
	EXPECT_EQ(summonedMarks(value), 0);
	value.arcaneConjuration = true;
	EXPECT_EQ(initialMarks(value), 2);
	EXPECT_EQ(summonedMarks(value), 2);
	value.rank = 0;
	EXPECT_EQ(initialMarks(value), 0);
	EXPECT_EQ(summonedMarks(value), 0);
}

TEST(NewHorizonsMetamagicMarks, AdeptAddsAfterTheResolvedApplicationWithoutIncreasingEntrySeeds)
{
	auto value = expert();
	value.adeptAdditionalMarks = 1;
	value.arcaneConjuration = value.perfectImprint = true;
	EXPECT_EQ(marksAfterResolvedSpell(value, 0), 2);
	value.residualMagic = true;
	EXPECT_EQ(marksAfterResolvedSpell(value, 0), 3);
	EXPECT_EQ(marksAfterResolvedSpell(value, 3), 5);
	EXPECT_EQ(initialMarks(value), 2);
	EXPECT_EQ(summonedMarks(value), 2);
}

TEST(NewHorizonsMetamagicMarks, ThresholdRidersUseTheExistingCountAndPreserveDurationSentinels)
{
	auto value = expert();
	value.lingeringMagic = value.arcaneVulnerability = value.arcaneFortification = true;
	EXPECT_EQ(additionalDuration(value, 2), 0);
	EXPECT_EQ(additionalDuration(value, 3), 2);
	EXPECT_EQ(scaleDuration(3, value, 3), 6);
	EXPECT_EQ(scaleDuration(0, value, 5), 0);
	EXPECT_EQ(scaleDuration(-1, value, 5), -1);
	EXPECT_EQ(resistanceReductionPoints(value, 2), 0);
	EXPECT_EQ(resistanceReductionPoints(value, 3), 20);
	EXPECT_EQ(enemySpellDamageReductionPercent(value, 2), 0);
	EXPECT_EQ(enemySpellDamageReductionPercent(value, 3), 20);
}

TEST(NewHorizonsMetamagicMarks, ProportionalManaRetainsFractionUntilFinalOrdinaryUpwardRounding)
{
	auto value = expert();
	value.manaAttunement = true;
	EXPECT_EQ(adjustedManaCost(200, value, 2, 4), 185); // 7.5%, not an early 7%.
	EXPECT_EQ(adjustedManaCost(80, value, 1, 4), 77); // 3.75%, not an early 3%.
	EXPECT_EQ(adjustedManaCost(20, value, 1, 2), 19);
	EXPECT_EQ(adjustedManaCost(20, value, 4, 4), 17);
	EXPECT_EQ(adjustedManaCost(1, value, 1, 1), 1);
	EXPECT_EQ(adjustedManaCost(0, value, 1, 1), 0);
	EXPECT_EQ(adjustedManaCost(31, value, 0, 0), 31);
	EXPECT_EQ(adjustedManaCost(31, value, 0, 4), 31);
}

TEST(NewHorizonsMetamagicMarks, NumericDomainsRejectMalformedCountsAndSaturateActualMagnitudeLimits)
{
	auto value = expert();
	EXPECT_THROW(effectivenessBonusPercent(value, -1), std::invalid_argument);
	EXPECT_THROW(effectivenessBonusPercent(value, 6), std::invalid_argument);
	EXPECT_THROW(adjustedManaCost(10, value, 2, 1), std::invalid_argument);
	EXPECT_THROW(scaleMagnitude(-1, value, 1), std::invalid_argument);
	EXPECT_EQ(scaleMagnitude(std::numeric_limits<int64_t>::max(), value, 5), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(scaleSignedBonus(std::numeric_limits<int32_t>::min(), value, 5), std::numeric_limits<int32_t>::min());
	EXPECT_EQ(scaleSignedBonus(-3, value, 1), -3);
	EXPECT_EQ(scaleSignedBonus(3, value, 1), 3);
	EXPECT_EQ(scaleSignedBonus(-100, value, 1), -115);
	EXPECT_EQ(scaleSignedBonus(std::numeric_limits<int32_t>::min(), value, 1),
		std::numeric_limits<int32_t>::min()); // Match ordinary signed timed-value floor.
	EXPECT_EQ(scaleDuration(std::numeric_limits<int32_t>::max(), value, 5), std::numeric_limits<int32_t>::max());
}

TEST(NewHorizonsMetamagicMarks, OverchargeIncludesTheSurchargeInTheSameExactFinalDiscount)
{
	auto value = expert();
	value.manaAttunement = true;
	EXPECT_EQ(maximumAffordableOvercharge(17, 10, 100, value, 1, 1), 10);
	EXPECT_EQ(adjustedManaCost(20, value, 1, 1), 17);
	EXPECT_EQ(adjustedManaCost(21, value, 1, 1), 18);
	EXPECT_EQ(maximumAffordableOvercharge(20, 10, 5, value, 1, 1), 5);
	EXPECT_EQ(maximumAffordableOvercharge(8, 10, 100, value, 1, 1), -1);
	EXPECT_EQ(maximumAffordableOvercharge(17, 10, 100, value, 0, 1), 7);
}


namespace
{
class MarkedRecipient : public UnitMock
{
public:
	int32_t metamagicMarks(BattleSide side) const override
	{
		return side == BattleSide::ATTACKER ? 4 : 1;
	}
};
}

TEST(NewHorizonsMetamagicMarks, EachOriginalHeroUsesOnlyTheirOwnCounter)
{
	testing::NiceMock<MarkedRecipient> recipient;
	CastProfile attacker{expert(), nullptr, BattleSide::ATTACKER};
	CastProfile defender{expert(), nullptr, BattleSide::DEFENDER};
	EXPECT_EQ(recipientBonusPercent(&attacker, &recipient), 60);
	EXPECT_EQ(recipientBonusPercent(&defender, &recipient), 15);
	EXPECT_EQ(outgoingMagnitude(100, &attacker, &recipient), 160);
	EXPECT_EQ(outgoingMagnitude(100, &defender, &recipient), 115);
	EXPECT_EQ(outgoingMagnitude(100, nullptr, &recipient), 100);
}

TEST(NewHorizonsMetamagicMarks, HandOfFateWeightsPreserveUniformAndFateDealerDistributions)
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
	Profile value = expert();
	value.manaAttunement = true;
	// Unmarked primary plus one qualified hostile among 4 collateral candidates.
	// Uniform expected fraction=1/8; Fate Dealer expected fraction=7/32.
	EXPECT_EQ(adjustedManaCost(200, value, 1, 8), 197);
	EXPECT_EQ(adjustedManaCost(200, value, 7, 32), 194);
}

TEST(NewHorizonsMetamagicMarks, FateManaUsesExpectedOutcomeFractionsIncludingBlockedCollateral)
{
	using newHorizonsHandOfFate::expectedManaFraction;
	auto value = expert();
	value.manaAttunement = true;
	const auto cost = [&value](bool primary, uint64_t denominator, uint64_t legal, uint64_t qualified)
	{
		const auto fraction = expectedManaFraction(primary, denominator, legal, qualified);
		return adjustedManaCost(200, value, fraction.first, fraction.second);
	};
	EXPECT_EQ(cost(false, 4, 0, 0), 200); // All blocked, unmarked primary.
	EXPECT_EQ(cost(true, 4, 0, 0), 170); // All blocked, marked primary alone.
	EXPECT_EQ(cost(true, 4, 4, 0), 185); // All legal, none of the collateral marked.
	EXPECT_EQ(cost(true, 4, 4, 4), 170); // Every possible recipient qualifies.
	EXPECT_EQ(cost(true, 4, 2, 1), 174); // E(fraction)=7/8, not E(Q)/E(N).
	EXPECT_EQ(cost(false, 4, 2, 1), 197); // E(fraction)=1/8.
	EXPECT_EQ(cost(true, 0, 0, 0), 170); // No secondary pool.
	EXPECT_THROW(expectedManaFraction(true, 4, 2, 3), std::invalid_argument);
}

TEST(NewHorizonsMetamagicMarks, NativeDamageCapPrecedesTheFinalFortificationOutputFactor)
{
	const auto value = expert();
	EXPECT_EQ(spellDamageOutput(5, value, 5, 20, 5), 4); // 5→8→cap5→4.
	EXPECT_EQ(spellDamageOutput(5, value, 5, 0, 5), 5);
	EXPECT_EQ(spellDamageOutput(5, value, 5, 20, 0), 6);
	EXPECT_EQ(spellDamageOutput(150, Profile{}, 0, 20, 0), 120); // No new physical Poison cap.
	EXPECT_EQ(spellDamageOutput(5, Profile{}, 0, 0, 0), 5);
}

TEST(NewHorizonsMetamagicMarks, FrailtyScalesEachNewFractionAndNeverReamplifiesItsCumulativeHistory)
{
	const auto value = expert();
	const auto castFraction = scaleMagnitude(2000, value, 5);
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

TEST(NewHorizonsMetamagicMarks, DivineRetributionSnapshotsRatioAndCapBeforeAnyFutureMarkGrowth)
{
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(200, 150, 100), 60);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(200, 262, 100, 5250), 105);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(1000, 262, 100, 5250), 262);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(1000, 262, 120, 5250), 314);
	EXPECT_EQ(newHorizonsMagic::divineRetributionPayout(7, 262, 100, 5250), 3);
	EXPECT_THROW(newHorizonsMagic::divineRetributionPayout(200, 262, 100, 6901), std::invalid_argument);
}

TEST(NewHorizonsMetamagicMarks, DivineRetributionDistinguishesHistoricalAbsenceFromCurrentRatioPresence)
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

TEST(NewHorizonsMetamagicMarks, ChaosShieldAmplifiesOnlyItsNumericalReductionUnderTheNativeCap)
{
	const auto profile = expert();
	EXPECT_EQ(chaosShieldMagnitude(5000, profile, 0, 8000), 5000);
	EXPECT_EQ(chaosShieldMagnitude(5000, profile, 5, 8000), 8000);
	EXPECT_EQ(chaosShieldMagnitude(6000, profile, 5, 9000), 9000);
}

TEST(NewHorizonsMetamagicMarks, MisfortuneAmplifiesReductionFromNeutralAndRetainsItsNativeFloor)
{
	const auto profile = expert();
	EXPECT_EQ(misfortuneChanceFactor(6500, profile, 0), 6500);
	EXPECT_EQ(misfortuneChanceFactor(6500, profile, 5), 3875);
	EXPECT_EQ(misfortuneChanceFactor(5000, profile, 5), 2500);
	EXPECT_EQ(misfortuneChanceFactor(2500, profile, 5), 2500);
}

TEST(NewHorizonsMetamagicPoints, CurrentCaptureRequiresIntegerThree)
{
	JsonNode rules;
	EXPECT_FALSE(usesPoints(rules));
	rules["metamagicPolicy"]["version"].Integer() = 3;
	EXPECT_TRUE(usesPoints(rules));
	EXPECT_FALSE(usesArcaneMarks(rules));
	rules["metamagicPolicy"]["version"].String() = "3";
	EXPECT_FALSE(usesPoints(rules));
}

TEST(NewHorizonsMetamagicPoints, RoundGrowthStartsAtOneOrThreeAndCannotExceedTwelve)
{
	auto value = expert();
	value.pointPolicy = true;
	EXPECT_EQ(pointsAtRoundStart(value, 0, 1), 1);
	value.arcaneWellspring = true;
	EXPECT_EQ(pointsAtRoundStart(value, 0, 1), 3);
	EXPECT_EQ(pointsAtRoundStart(value, 3, 2), 4);
	EXPECT_EQ(pointsAtRoundStart(value, 12, 20), 12);
	EXPECT_THROW(pointsAtRoundStart(value, 13, 20), std::invalid_argument);
	EXPECT_THROW(pointsAtRoundStart(value, 0, 0), std::invalid_argument);
}

TEST(NewHorizonsMetamagicPoints, ManualOwnAndEnemyCompletionAreDistinctAndAutomaticNeverGeneratesPoints)
{
	auto value = expert();
	value.pointPolicy = true;
	value.arcaneCatalyst = true;
	EXPECT_EQ(pointsAfterResolvedSpell(value, 5, true, true), 6);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 5, false, true), 5);
	value.arcaneFeedback = true;
	EXPECT_EQ(pointsAfterResolvedSpell(value, 5, false, true), 6);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 5, true, false), 5);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 5, false, false), 5);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 12, true, true), 12);
}

TEST(NewHorizonsMetamagicPoints, SnapshotScalesEveryRecipientEquallyAndKeepsNativeDamageCap)
{
	CastProfile cast;
	cast.perks = expert();
	cast.perks.pointPolicy = true;
	cast.points = 8;
	EXPECT_EQ(castCounter(cast, 0), 8);
	EXPECT_EQ(castCounter(cast, 5), 8);
	EXPECT_EQ(effectivenessBonusPercent(cast.perks, cast.points), 24);
	EXPECT_EQ(scaleMagnitude(101, cast.perks, cast.points), 125);
	EXPECT_EQ(spellDamageOutput(100, cast.perks, cast.points, 0, 110), 110);
	EXPECT_EQ(spellDamageOutput(100, cast.perks, cast.points, 0, 0, 35), 159);
	EXPECT_EQ(marksAfterResolvedSpell(cast.perks, 5), 5);
}

TEST(NewHorizonsMetamagicPoints, RoundRidersUsePostGrowthPointsAndFloorAtTheirOwnThresholds)
{
	auto value = expert();
	value.pointPolicy = true;
	value.manaConduit = value.arcaneAttunement = value.arcaneShield = value.lingeringMagic = true;
	EXPECT_EQ(manaConduitRecovery(value, 2), 0);
	EXPECT_EQ(manaConduitRecovery(value, 3), 1);
	EXPECT_EQ(attunementInitiative(value, 3), 0);
	EXPECT_EQ(attunementInitiative(value, 4), 1);
	EXPECT_EQ(attunementInitiative(value, 12), 3);
	EXPECT_EQ(arcaneShieldCapacity(value, 3, 199), 5);
	EXPECT_EQ(additionalDuration(value, 8), 2);
	EXPECT_EQ(scaleDuration(10, value, 8), 14);
	EXPECT_EQ(scaleDuration(-1, value, 12), -1);
}

TEST(NewHorizonsMetamagicPoints, SingularityDiscountIsLatchedAndRoundedUpAndEchoIsExactQuarter)
{
	auto value = expert();
	value.pointPolicy = value.arcaneSingularity = true;
	EXPECT_EQ(singularityManaCost(7, value, false), 7);
	EXPECT_EQ(singularityManaCost(7, value, true), 6);
	EXPECT_EQ(singularityManaCost(0, value, true), 0);
	EXPECT_EQ(maximumAffordableOvercharge(7, 7, 20, value, 0, 0, true), 2);
	EXPECT_EQ(maximumAffordableOvercharge(7, 7, 20, value, 0, 0, false), 0);
	EXPECT_EQ(maximumAffordableOvercharge(5, 7, 20, value, 0, 0, true), -1);
	EXPECT_EQ(singularityManaCost(std::numeric_limits<int32_t>::max(), value, true), 1610612736);
	EXPECT_EQ(echoDamage(103), 25);
	EXPECT_EQ(echoDamage(3), 0);
	EXPECT_THROW(echoDamage(-1), std::invalid_argument);
}

TEST(NewHorizonsMetamagicPoints, CurrentPointStateRoundTripsWithoutDroppingBattleLatches)
{
	MetamagicPointState original{12, true, true};
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_FACTION_SKILL_REVISIONS;
	bytes.iser.version = bytes.oser.version;
	bytes.oser & original;
	MetamagicPointState restored;
	bytes.iser & restored;
	EXPECT_EQ(restored, original);
	original.points = 13;
	EXPECT_THROW(original.validate(), std::runtime_error);
	original = {9, true, false};
	EXPECT_THROW(original.validate(), std::runtime_error);
}

TEST(NewHorizonsMetamagicPoints, CompletionPacketRejectsWrongRoundAndDecreasingOrClearedState)
{
	const MetamagicPointState previous{10, true, true};
	SetMetamagicPointState update;
	update.battleID = BattleID(0);
	update.side = BattleSide::ATTACKER;
	update.round = 5;
	update.state = {11, true, true};
	EXPECT_NO_THROW(update.validateTransitionFrom(previous, 5));
	EXPECT_THROW(update.validateTransitionFrom(previous, 6), std::runtime_error);
	update.state.points = 9;
	EXPECT_THROW(update.validateTransitionFrom(previous, 5), std::runtime_error);
	update.state = {11, false, true};
	EXPECT_THROW(update.validateTransitionFrom(previous, 5), std::runtime_error);
	update.state = {11, true, false};
	EXPECT_THROW(update.validateTransitionFrom(previous, 5), std::runtime_error);
	update.side = BattleSide::NONE;
	EXPECT_THROW(update.validateShape(), std::runtime_error);
}

TEST(NewHorizonsMetamagicPoints, AdeptOwnManualCastAddsOneWithoutRequiringCatalyst)
{
	auto value = expert();
	value.pointPolicy = true;
	value.adeptAdditionalPoints = 1;
	EXPECT_EQ(pointsAfterResolvedSpell(value, 2, true, true), 3);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 2, false, true), 2);
	EXPECT_EQ(pointsAfterResolvedSpell(value, 2, true, false), 2);
	value.arcaneCatalyst = true;
	EXPECT_EQ(pointsAfterResolvedSpell(value, 2, true, true), 4);
}
