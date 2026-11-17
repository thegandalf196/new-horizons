/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <stdexcept>

namespace
{
using namespace consecratedArms;
Policy withPerk(Perk perk, int rank = 3)
{
	return {rank, static_cast<uint16_t>(perk), (rank - 1) * 50, 100};
}
}

TEST(ConsecratedArmsRules, FlatRankFaithDoesNotChangeDuration)
{
	for(int rank = 1; rank <= 3; ++rank)
	{
		EXPECT_EQ(potencyFaith(200, Policy{rank, 0, (rank - 1) * 50, 100}), 200 + (rank - 1) * 50);
		EXPECT_EQ(duration(200, Policy{rank}), 3);
		EXPECT_EQ(potencyFaith(200, withPerk(Perk::FERVENT_INVOCATION, rank)), 300 + (rank - 1) * 50);
		EXPECT_EQ(duration(200, withPerk(Perk::FERVENT_INVOCATION, rank)), 3);
	}
}

TEST(ConsecratedArmsRules, InitialReferencesAndCaps)
{
	const std::array<int, 3> faith{100, 200, 400};
	const std::array<int, 3> attributes{1, 2, 3};
	for(size_t index = 0; index < faith.size(); ++index)
	{
		const auto result = values(faith[index], Policy{1}, 1);
		EXPECT_EQ(result.attributes, attributes[index]);
		EXPECT_EQ(result.retaliationReductionBP, 2000 + 10 * faith[index]);
		EXPECT_EQ(result.resistanceBP, 1000 + 5 * faith[index]);
		EXPECT_EQ(result.redemptionConversionBP, 1000 + 10 * faith[index]);
		EXPECT_EQ(result.casualtyCeilingBP, 7000 - 5 * faith[index]);
		EXPECT_EQ(result.judgmentBP, 500 + 5 * faith[index]);
	}
	const auto capped = values(std::numeric_limits<int>::max(), Policy{3}, 6);
	EXPECT_EQ(capped.attributes, 3);
	EXPECT_EQ(capped.retaliationReductionBP, 6000);
	EXPECT_EQ(capped.resistanceBP, 4000);
	EXPECT_EQ(capped.redemptionConversionBP, 5000);
	EXPECT_EQ(capped.casualtyCeilingBP, 4000);
	EXPECT_EQ(capped.judgmentBP, 2500);
}

TEST(ConsecratedArmsRules, DurationUsesThresholdsAndEnduringAfterCap)
{
	EXPECT_EQ(duration(149, Policy{1}), 2);
	EXPECT_EQ(duration(150, Policy{1}), 3);
	EXPECT_EQ(duration(299, Policy{1}), 3);
	EXPECT_EQ(duration(300, Policy{1}), 4);
	EXPECT_EQ(duration(10000, withPerk(Perk::ENDURING_PRAYER)), 5);
	EXPECT_EQ(duration(0, withPerk(Perk::ENDURING_PRAYER)), 3);
}

TEST(ConsecratedArmsRules, UniformSamplingWithoutReplacementHasNoRetry)
{
	std::array<int, 6> inclusion{};
	for(int first = 0; first < 6; ++first)
	{
		const auto single = selectPrayers(Policy{1}, first);
		ASSERT_EQ(single.size(), 1);
		EXPECT_EQ(single[0], static_cast<Prayer>(first));
		for(int second = 0; second < 5; ++second)
		{
			const auto pair = selectPrayers(withPerk(Perk::ANTIPHONAL_PRAYER), first, second);
			ASSERT_EQ(pair.size(), 2);
			EXPECT_NE(pair[0], pair[1]);
			++inclusion[static_cast<size_t>(pair[0])];
			++inclusion[static_cast<size_t>(pair[1])];
		}
	}
	for(const auto count : inclusion)
		EXPECT_EQ(count, 10);
	EXPECT_THROW(selectPrayers(Policy{1}, 6), std::runtime_error);
}

TEST(ConsecratedArmsRules, SacredAccordAmplifiesBenefitThenCapsWithoutFeedback)
{
	const auto policy = withPerk(Perk::SACRED_ACCORD);
	for(int count = 1; count <= 6; ++count)
	{
		const int multiplier = 100 + 10 * std::min(count - 1, 3);
		const auto result = values(200, policy, count);
		EXPECT_EQ(result.redemptionConversionBP, 3000 * multiplier / 100);
		EXPECT_EQ(result.casualtyCeilingBP, 10000 - 4000 * multiplier / 100);
		EXPECT_EQ(result.judgmentBP, 1500 * multiplier / 100);
		EXPECT_EQ(result.attributes, 2); // floor the final integer magnitude.
	}
	const auto joyful = values(400, withPerk(Perk::JOYFUL_DEVOTION), 6);
	EXPECT_EQ(joyful.attributes, 3);
	EXPECT_EQ(joyful.moraleLuck, 4);
}

TEST(ConsecratedArmsRules, StrictOnePerTierDoesNotPermitExpertPair)
{
	Policy illegal{3, static_cast<uint16_t>(Perk::ANTIPHONAL_PRAYER)
		| static_cast<uint16_t>(Perk::LIVING_LITURGY)};
	EXPECT_THROW(illegal.validate(), std::runtime_error);
	Policy legal{3, static_cast<uint16_t>(Perk::ENDURING_PRAYER)
		| static_cast<uint16_t>(Perk::FERVENT_INVOCATION)
		| static_cast<uint16_t>(Perk::ANTIPHONAL_PRAYER)};
	EXPECT_NO_THROW(legal.validate());
}

TEST(ConsecratedArmsRules, VowRoundsUpAndAllowsSingleCreatureToDie)
{
	EXPECT_EQ(casualtyLimit(1, 4000), 1);
	EXPECT_EQ(casualtyLimit(100, 6000), 60);
	EXPECT_EQ(casualtyLimit(101, 6000), 61);
	EXPECT_EQ(casualtyLimit(0, 6000), 0);
	EXPECT_EQ(casualtyLimit(std::numeric_limits<int64_t>::max(), 10000), std::numeric_limits<int64_t>::max());
}

TEST(ConsecratedArmsRules, RighteousSentenceAmplifiesOnlyCompletedTargets)
{
	EXPECT_EQ(judgmentDamage(1000, 1500, false, true), 150);
	EXPECT_EQ(judgmentDamage(1000, 1500, true, false), 150);
	EXPECT_EQ(judgmentDamage(1000, 1500, true, true), 225);
	EXPECT_EQ(judgmentDamage(20000, 1505, true, true), 4515);
	EXPECT_EQ(judgmentDamage(0, 1500, true, true), 0);
	EXPECT_EQ(judgmentDamage(std::numeric_limits<int64_t>::max(), 2500, true, true),
		std::numeric_limits<int64_t>::max() / 10000 * 3750
		+ std::numeric_limits<int64_t>::max() % 10000 * 3750 / 10000);
}
