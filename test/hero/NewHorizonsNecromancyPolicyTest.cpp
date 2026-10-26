/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/json/JsonNode.h"

#include <limits>
#include <stdexcept>

namespace
{
JsonNode capturedPolicy()
{
	JsonNode rules;
	auto & row = rules[newHorizonsNecromancy::POLICY_KEY];
	row["version"].Integer() = 1;
	for(int value : {0, 50, 60, 70})
		row["skeletonLeadershipReductionPercent"].Vector().emplace_back(value);
	row["lordOfTheDeadLeadershipReductionPercent"].Integer() = 90;
	row["deathMarchSpeed"].Integer() = 2;
	row["bonewardMagicalReductionPercent"].Integer() = 30;
	row["tombGuardAdditionalRetaliations"].Integer() = 1;
	return rules;
}
}

TEST(NewHorizonsNecromancyPolicy, AbsentCaptureDoesNotAdoptInstalledDefaults)
{
	EXPECT_FALSE(newHorizonsNecromancy::policy(JsonNode()));
	JsonNode historical;
	historical["schemaVersion"].Integer() = 1;
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical));
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(historical, false));
	const auto authored = capturedPolicy();
	EXPECT_TRUE(newHorizonsNecromancy::policy(authored));
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical));
}

TEST(NewHorizonsNecromancyPolicy, CapturedCoefficientsOwnTheirValues)
{
	auto rules = capturedPolicy();
	const auto captured = newHorizonsNecromancy::policy(rules);
	ASSERT_TRUE(captured);
	EXPECT_EQ(captured->skeletonLeadershipReductionPercent, (std::array<int, 4>{0, 50, 60, 70}));
	EXPECT_EQ(captured->lordOfTheDeadLeadershipReductionPercent, 90);
	EXPECT_EQ(captured->deathMarchSpeed, 2);
	EXPECT_EQ(captured->bonewardMagicalReductionPercent, 30);
	EXPECT_EQ(captured->tombGuardAdditionalRetaliations, 1);
	rules[newHorizonsNecromancy::POLICY_KEY]["deathMarchSpeed"].Integer() = 5;
	EXPECT_EQ(captured->deathMarchSpeed, 2);
	EXPECT_EQ(newHorizonsNecromancy::policy(rules)->deathMarchSpeed, 5);
}

TEST(NewHorizonsNecromancyPolicy, RejectsUnsupportedSerializationWithoutDiscardingPolicy)
{
	const auto rules = capturedPolicy();
	EXPECT_THROW(newHorizonsNecromancy::validatePolicySerialization(rules, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(rules, true));
	EXPECT_EQ(newHorizonsNecromancy::policy(rules)->deathMarchSpeed, 2);
}

TEST(NewHorizonsNecromancyPolicy, RejectsMissingUnknownAndUnsupportedFields)
{
	for(const auto * key : {"version", "skeletonLeadershipReductionPercent",
		"lordOfTheDeadLeadershipReductionPercent", "deathMarchSpeed",
		"bonewardMagicalReductionPercent", "tombGuardAdditionalRetaliations"})
	{
		SCOPED_TRACE(key);
		auto rules = capturedPolicy();
		rules[newHorizonsNecromancy::POLICY_KEY].Struct().erase(key);
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	auto unknown = capturedPolicy();
	unknown[newHorizonsNecromancy::POLICY_KEY]["extra"].Integer() = 1;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(unknown), std::runtime_error);
	auto future = capturedPolicy();
	future[newHorizonsNecromancy::POLICY_KEY]["version"].Integer() = 2;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(future), std::runtime_error);
	JsonNode nullPolicy;
	nullPolicy[newHorizonsNecromancy::POLICY_KEY] = JsonNode();
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(nullPolicy), std::runtime_error);
}

TEST(NewHorizonsNecromancyPolicy, RejectsMalformedRanksAndFractionalOrNonfiniteCoefficients)
{
	for(int count : {0, 3, 5})
	{
		auto rules = capturedPolicy();
		rules[newHorizonsNecromancy::POLICY_KEY]["skeletonLeadershipReductionPercent"].Vector().resize(count);
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	auto unlearned = capturedPolicy();
	unlearned[newHorizonsNecromancy::POLICY_KEY]["skeletonLeadershipReductionPercent"].Vector()[0].Integer() = 1;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(unlearned), std::runtime_error);
	for(double value : {-1.0, 100.0, 50.5, std::numeric_limits<double>::infinity()})
	{
		auto rules = capturedPolicy();
		rules[newHorizonsNecromancy::POLICY_KEY]["skeletonLeadershipReductionPercent"].Vector()[1].Float() = value;
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	for(const auto * key : {"lordOfTheDeadLeadershipReductionPercent", "deathMarchSpeed",
		"bonewardMagicalReductionPercent", "tombGuardAdditionalRetaliations"})
		for(double value : {-1.0, 101.0, 1.5, std::numeric_limits<double>::quiet_NaN()})
		{
			SCOPED_TRACE(key);
			auto rules = capturedPolicy();
			rules[newHorizonsNecromancy::POLICY_KEY][key].Float() = value;
			EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
		}
	for(const auto * key : {"lordOfTheDeadLeadershipReductionPercent", "deathMarchSpeed"})
	{
		auto rules = capturedPolicy();
		rules[newHorizonsNecromancy::POLICY_KEY][key].Bool() = true;
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	auto freeLeadership = capturedPolicy();
	freeLeadership[newHorizonsNecromancy::POLICY_KEY]["lordOfTheDeadLeadershipReductionPercent"].Integer() = 100;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(freeLeadership), std::runtime_error);
}

TEST(NewHorizonsNecromancyPolicy, DiscountRoundsUpOnceAndPreservesPositiveCosts)
{
	using newHorizonsNecromancy::discountedLeadershipRequirement;
	EXPECT_EQ(discountedLeadershipRequirement(45, 0), 45);
	EXPECT_EQ(discountedLeadershipRequirement(45, 50), 23);
	EXPECT_EQ(discountedLeadershipRequirement(45, 60), 18);
	EXPECT_EQ(discountedLeadershipRequirement(45, 70), 14);
	EXPECT_EQ(discountedLeadershipRequirement(45, 90), 5);
	EXPECT_EQ(discountedLeadershipRequirement(1, 99), 1);
	EXPECT_EQ(discountedLeadershipRequirement(std::numeric_limits<int>::max(), 99), 21474837);
	EXPECT_EQ(discountedLeadershipRequirement(std::numeric_limits<int>::max(), 0), std::numeric_limits<int>::max());
	for(int base = 1; base <= 100; ++base)
		for(int reduction = 0; reduction < 100; ++reduction)
		{
			const int discounted = discountedLeadershipRequirement(base, reduction);
			EXPECT_GE(discounted * 100, base * (100 - reduction));
			EXPECT_LT((discounted - 1) * 100, base * (100 - reduction));
		}
}

TEST(NewHorizonsNecromancyPolicy, InvalidDiscountDoesNotProduceUncappedZeroRequirement)
{
	using newHorizonsNecromancy::discountedLeadershipRequirement;
	EXPECT_THROW(discountedLeadershipRequirement(0, 50), std::invalid_argument);
	EXPECT_THROW(discountedLeadershipRequirement(-1, 50), std::invalid_argument);
	EXPECT_THROW(discountedLeadershipRequirement(45, -1), std::invalid_argument);
	EXPECT_THROW(discountedLeadershipRequirement(45, 100), std::invalid_argument);
}
