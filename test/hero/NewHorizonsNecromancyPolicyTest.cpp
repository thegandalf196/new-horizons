/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/entities/hero/NewHorizonsCommandAdmission.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"

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
	row["graveplatePhysicalReductionPercent"].Integer() = 20;
	row["graveKnowledgeRecoveryPercent"].Integer() = 20;
	return rules;
}
}

TEST(NewHorizonsNecromancyPolicy, AbsentCaptureDoesNotAdoptInstalledDefaults)
{
	EXPECT_FALSE(newHorizonsNecromancy::policy(JsonNode()));
	JsonNode historical;
	historical["schemaVersion"].Integer() = 1;
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical));
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(historical, false, false));
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
	EXPECT_EQ(captured->graveplatePhysicalReductionPercent, 20);
	EXPECT_EQ(captured->graveKnowledgeRecoveryPercent, 20);
	rules[newHorizonsNecromancy::POLICY_KEY]["deathMarchSpeed"].Integer() = 5;
	EXPECT_EQ(captured->deathMarchSpeed, 2);
	EXPECT_EQ(newHorizonsNecromancy::policy(rules)->deathMarchSpeed, 5);
}

TEST(NewHorizonsNecromancyPolicy, RejectsUnsupportedSerializationWithoutDiscardingPolicy)
{
	const auto rules = capturedPolicy();
	EXPECT_THROW(newHorizonsNecromancy::validatePolicySerialization(rules, false, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(rules, true, false));
	EXPECT_EQ(newHorizonsNecromancy::policy(rules)->deathMarchSpeed, 2);
}

TEST(NewHorizonsNecromancyPolicy, RejectsMissingUnknownAndUnsupportedFields)
{
	for(const auto * key : {"version", "skeletonLeadershipReductionPercent",
		"lordOfTheDeadLeadershipReductionPercent", "deathMarchSpeed",
		"bonewardMagicalReductionPercent", "tombGuardAdditionalRetaliations",
		"graveplatePhysicalReductionPercent", "graveKnowledgeRecoveryPercent"})
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
	future[newHorizonsNecromancy::POLICY_KEY]["version"].Integer() = 4;
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
		"bonewardMagicalReductionPercent", "tombGuardAdditionalRetaliations",
		"graveplatePhysicalReductionPercent", "graveKnowledgeRecoveryPercent"})
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

TEST(NewHorizonsNecromancyPolicy, VersionTwoFieldsAndSerializationRemainDistinctFromHistoricalPolicy)
{
	auto historical = capturedPolicy();
	auto current = historical;
	auto & row = current[newHorizonsNecromancy::POLICY_KEY];
	row["version"].Integer() = 2;
	row["bonewardMagicalReductionPercent"].Integer() = 0;
	row["tombGuardAdditionalRetaliations"].Integer() = 0;
	row["bonewardMagicResistancePercent"].Integer() = 50;
	row["tombGuardDoubleStrike"].Bool() = true;
	row["deadMansLuckBonus"].Integer() = 3;
	row["boneCollectorRaiseBonusPercent"].Integer() = 50;
	row["soulHarvesterManaPerRaisedCreature"].Integer() = 1;
	row["soulHarvesterManaCap"].Integer() = 50;
	ASSERT_TRUE(newHorizonsNecromancy::policy(current));
	EXPECT_TRUE(newHorizonsNecromancy::policy(current)->tombGuardDoubleStrike);
	EXPECT_EQ(newHorizonsNecromancy::policy(current)->version, 2);
	EXPECT_EQ(newHorizonsNecromancy::policy(current)->lordOfTheDeadMaximumHealthPercent, 0);
	EXPECT_EQ(newHorizonsNecromancy::policy(current)->lordOfTheDeadLeadershipReductionPercent, 90);
	EXPECT_EQ(newHorizonsNecromancy::policy(current)->skeletonLeadershipReductionPercent, (std::array<int, 4>{0, 50, 60, 70}));
	EXPECT_NO_THROW(newHorizonsHeroes::validateCommandPerkSerialization(current, false));
	EXPECT_FALSE(newHorizonsNecromancy::policy(historical)->tombGuardDoubleStrike);
	EXPECT_EQ(newHorizonsNecromancy::policy(historical)->tombGuardAdditionalRetaliations, 1);
	EXPECT_THROW(newHorizonsNecromancy::validatePolicySerialization(current, true, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsNecromancy::validatePolicySerialization(current, true, true));
	CMemorySerializer memory;
	memory.oser & current;
	JsonNode restored;
	memory.iser & restored;
	EXPECT_EQ(restored, current);
	EXPECT_EQ(newHorizonsNecromancy::policy(restored), newHorizonsNecromancy::policy(current));
	for(const auto * key : {"bonewardMagicResistancePercent", "tombGuardDoubleStrike", "deadMansLuckBonus",
		"boneCollectorRaiseBonusPercent", "soulHarvesterManaPerRaisedCreature", "soulHarvesterManaCap"})
	{
		auto missing = current;
		missing[newHorizonsNecromancy::POLICY_KEY].Struct().erase(key);
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(missing), std::runtime_error);
		auto oldWithNewField = historical;
		oldWithNewField[newHorizonsNecromancy::POLICY_KEY][key] = row[key];
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(oldWithNewField), std::runtime_error);
	}
	row["tombGuardAdditionalRetaliations"].Integer() = 1;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(current), std::runtime_error);
	row["tombGuardAdditionalRetaliations"].Integer() = 0;
	row["bonewardMagicalReductionPercent"].Integer() = 30;
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(current), std::runtime_error);
}

TEST(NewHorizonsNecromancyPolicy, ActualShippedRegistryPassesNativeValidationAndPreparationWithPolicyThree)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
	EXPECT_NE(newHorizonsHeroes::preparePilotPerkRules(rules), nullptr);
	const auto captured = newHorizonsNecromancy::policy(rules);
	ASSERT_TRUE(captured);
	EXPECT_EQ(captured->version, 3);
	EXPECT_EQ(captured->lordOfTheDeadMaximumHealthPercent, 25);
	EXPECT_EQ(captured->skeletonLeadershipReductionPercent, (std::array<int, 4>{0, 0, 0, 0}));
	EXPECT_EQ(captured->lordOfTheDeadLeadershipReductionPercent, 0);
	EXPECT_FALSE(rules["necromancyPolicy"].Struct().contains("skeletonLeadershipReductionPercent"));
	EXPECT_FALSE(rules["necromancyPolicy"].Struct().contains("lordOfTheDeadLeadershipReductionPercent"));
	newHorizonsHeroes::PerkState state;
	state.rules = rules;
	ASSERT_NO_THROW(state.validate());
	CMemorySerializer memory;
	memory.oser & state;
	newHorizonsHeroes::PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.rules, rules);
	EXPECT_EQ(newHorizonsNecromancy::policy(restored.rules), captured);
}

TEST(NewHorizonsNecromancyPolicy, VersionThreeRejectsRetiredFieldsAndMalformedHealthWithoutChangingHistory)
{
	const JsonNode shipped(JsonPath::builtin("config/newHorizonsPerks"));
	for(const auto * key : {"skeletonLeadershipReductionPercent", "lordOfTheDeadLeadershipReductionPercent"})
	{
		auto rules = shipped;
		rules["necromancyPolicy"][key] = capturedPolicy()["necromancyPolicy"][key];
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	for(const JsonNode value : {JsonNode(), JsonNode(-1), JsonNode(25.5), JsonNode(true), JsonNode(1000001)})
	{
		auto rules = shipped;
		rules["necromancyPolicy"]["lordOfTheDeadMaximumHealthPercent"] = value;
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(rules), std::runtime_error);
	}
	auto missing = shipped;
	missing["necromancyPolicy"].Struct().erase("lordOfTheDeadMaximumHealthPercent");
	EXPECT_THROW(newHorizonsNecromancy::validatePolicy(missing), std::runtime_error);
	for(int version : {1, 2})
	{
		auto historical = shipped;
		historical["necromancyPolicy"]["version"].Integer() = version;
		EXPECT_THROW(newHorizonsNecromancy::validatePolicy(historical), std::runtime_error);
	}
	auto unmarked = shipped;
	unmarked.Struct().erase("commandAuthorityPolicy");
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(unmarked), std::runtime_error);
	const auto historical = newHorizonsNecromancy::policy(capturedPolicy());
	ASSERT_TRUE(historical);
	EXPECT_EQ(historical->lordOfTheDeadMaximumHealthPercent, 0);
	EXPECT_EQ(historical->lordOfTheDeadLeadershipReductionPercent, 90);
}

TEST(NewHorizonsNecromancyPolicy, VersionThreeRequiresItsEpochEvenWithoutTheCommandMarker)
{
	const JsonNode shipped(JsonPath::builtin("config/newHorizonsPerks"));
	JsonNode bare;
	bare["necromancyPolicy"] = shipped["necromancyPolicy"];
	EXPECT_THROW(newHorizonsHeroes::validateCommandPerkSerialization(bare, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateCommandPerkSerialization(bare, true));
	EXPECT_NO_THROW(newHorizonsHeroes::validateCommandPerkSerialization(capturedPolicy(), false));
	const auto previous = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	newHorizonsHeroes::PerkState state;
	state.rules = shipped;
	ASSERT_NO_THROW(state.validate());
	CMemorySerializer writer;
	writer.oser.version = previous;
	EXPECT_THROW(state.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	// An old-format reader must refuse before installing either rules or choices.
	newHorizonsHeroes::PerkState destination;
	const auto before = destination.toJson();
	CMemorySerializer reader;
	reader.oser & shipped;
	reader.iser.version = previous;
	EXPECT_THROW(destination.serialize(reader.iser), std::runtime_error);
	EXPECT_EQ(destination.toJson(), before);
	GameSettings settings;
	JsonNode overrides;
	overrides["heroes"]["newHorizonsPerks"] = bare;
	EXPECT_THROW(newHorizonsHeroes::validateCommandOverrides(overrides, false), std::runtime_error);
	const auto settingsBefore = settings.getFullConfig();
	CMemorySerializer settingsReader;
	settingsReader.oser & overrides;
	settingsReader.iser.version = previous;
	EXPECT_THROW(settings.serialize(settingsReader.iser), std::runtime_error);
	EXPECT_EQ(settings.getFullConfig(), settingsBefore);
}
