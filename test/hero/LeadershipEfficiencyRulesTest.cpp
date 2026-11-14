/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/GameSettings.h"
#include <array>
#include <limits>

TEST(LeadershipEfficiencyRules, Capacity100ApprovedExamplesAndDiminishingMarginalReturns)
{
	using namespace newHorizonsHeroes;
	const std::array<std::pair<int64_t, double>, 6> examples = {{{100, 100.0}, {150, 140.8248290463863},
		{200, 170.7106781186548}, {300, 215.4700538379252}, {500, 278.8854381999832}, {1000, 384.6049894151541}}};
	for(const auto & [count, expected] : examples)
		EXPECT_NEAR(count * squareRootLeadershipEfficiency(count, 100), expected, 1e-10);
	double previous = 100, previousMarginal = 1;
	for(int64_t count = 101; count <= 100000; ++count)
	{
		const double current = count * squareRootLeadershipEfficiency(count, 100);
		const double marginal = current - previous;
		EXPECT_GT(marginal, 0);
		EXPECT_LE(marginal, previousMarginal + 1e-9);
		previous = current;
		previousMarginal = marginal;
	}
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(1, 0), 1);
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(0, 100), 1);
	EXPECT_GT(squareRootLeadershipEfficiency(std::numeric_limits<int32_t>::max(), 1), 0);
}

TEST(LeadershipEfficiencyRules, AdditivePercentagesRoundOnceAndSaturateWideInputs)
{
	using newHorizonsHeroes::leadershipWithBonusPercent;
	EXPECT_EQ(leadershipWithBonusPercent(1001, 45 + 25 + 35 + 50), 2552);
	EXPECT_EQ(leadershipWithBonusPercent(1001, 30), 1301);
	EXPECT_EQ(leadershipWithBonusPercent(1001, 0), 1001);
	EXPECT_EQ(leadershipWithBonusPercent(0, 45), 0);
	EXPECT_EQ(leadershipWithBonusPercent(std::numeric_limits<int64_t>::max(), 45), std::numeric_limits<int64_t>::max());
	EXPECT_THROW(leadershipWithBonusPercent(1000, -1), std::invalid_argument);
}

TEST(LeadershipEfficiencyRules, All18ClassesUseTheirActualMartialGrowth)
{
	using namespace newHorizonsHeroes;
	const JsonNode world(JsonPath::builtin("config/newHorizonsCapabilities"));
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_EQ(world["rulesetVersion"].Integer(), 7);
	ASSERT_EQ(world["classProfiles"].Struct().size(), 18u);
	ASSERT_NO_THROW(validateCapabilityRules(world, true));
	for(const auto & [key, profile] : world["classProfiles"].Struct())
	{
		SCOPED_TRACE(key);
		const auto & growth = development["classProfiles"][key]["growth"].Vector();
		const int64_t martial = growth[0].Integer() + growth[1].Integer();
		EXPECT_EQ(profile["base"].Integer(), 500 + 75 * martial);
		EXPECT_EQ(profile["perLevel"].Integer(), 20 + 5 * martial);
		const auto captured = resolveCapabilityRules(world, HeroClassID(HeroClassID::decode(key)));
		for(const int level : {1, 2, 9, 20, 100})
			EXPECT_EQ(capabilityLeadershipRating(captured, level), 500 + 75 * martial + (level - 1) * (20 + 5 * martial));
		EXPECT_FALSE(captured["leadership"]["resolvedCreatureRequirements"].Struct().empty());
		EXPECT_NO_THROW(validateResolvedCapabilityRules(captured));
	}
}

TEST(LeadershipEfficiencyRules, ResolvedRequirementsPreserveUpgradeParityAndExplicitExceptions)
{
	using namespace newHorizonsHeroes;
	JsonNode world(JsonPath::builtin("config/newHorizonsCapabilities"));
	const CreatureID base(CreatureID::SKELETON), upgraded(CreatureID::decode("core:skeletonWarrior"));
	const auto knight = HeroClassID(HeroClassID::decode("core:knight"));
	auto captured = resolveCapabilityRules(world, knight);
	EXPECT_EQ(capabilityCreatureLeadershipRequirement(captured, upgraded), capabilityCreatureLeadershipRequirement(captured, base));
	world["leadership"]["creatureRequirements"]["core:skeletonWarrior"].Integer() = 77;
	captured = resolveCapabilityRules(world, knight);
	EXPECT_EQ(capabilityCreatureLeadershipRequirement(captured, upgraded), 77);
	auto forged = captured;
	forged["leadership"]["resolvedCreatureRequirements"]["core:skeletonWarrior"].Integer() = 1;
	EXPECT_THROW(validateResolvedCapabilityRules(forged), std::runtime_error);
}

TEST(LeadershipEfficiencyRules, NecromancyDiscountsUseUpwardIntegerRoundingWithoutLayering)
{
	using newHorizonsNecromancy::discountedLeadershipRequirement;
	EXPECT_EQ(discountedLeadershipRequirement(45, 0), 45);
	EXPECT_EQ(discountedLeadershipRequirement(45, 50), 23);
	EXPECT_EQ(discountedLeadershipRequirement(45, 60), 18);
	EXPECT_EQ(discountedLeadershipRequirement(45, 70), 14);
	EXPECT_EQ(discountedLeadershipRequirement(45, 90), 5);
	EXPECT_EQ(discountedLeadershipRequirement(1, 90), 1);
}

TEST(LeadershipEfficiencyRules, CompositionRoundtripAndDownlevelPrefixRefusal)
{
	using newHorizonsHeroes::LeadershipComposition;
	const LeadershipComposition original{true, true, true};
	CMemorySerializer wire;
	wire.oser & original;
	LeadershipComposition restored;
	wire.iser & restored;
	EXPECT_EQ(restored, original);
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	EXPECT_THROW(older.oser & original, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
	LeadershipComposition invalid{false, true, false};
	EXPECT_THROW(invalid.validate(), std::runtime_error);
}

TEST(LeadershipEfficiencyRules, DownlevelSettingsRejectBeforePrefixAndPreservePriorStateOnRead)
{
	const JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
	GameSettings authored;
	authored.addOverride(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	EXPECT_THROW(authored.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	GameSettings recipient;
	const auto before = recipient.getFullConfig();
	JsonNode incoming;
	incoming["heroes"]["newHorizonsCapabilities"] = capabilities;
	CMemorySerializer reader;
	reader.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	reader.iser.version = reader.oser.version;
	reader.oser & incoming;
	EXPECT_THROW(recipient.serialize(reader.iser), std::runtime_error);
	EXPECT_EQ(recipient.getFullConfig(), before);
}
