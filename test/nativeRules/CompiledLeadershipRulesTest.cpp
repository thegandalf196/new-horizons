/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../hero/HistoricalCapabilityRules.h"
#include "../../lib/nativeRules/CompiledCapabilityRules.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"

using CompiledLeadershipRules = vcmiTestData::OriginalDataTest;

TEST_F(CompiledLeadershipRules, HistoricalAndCurrentRatingAndEveryCreatureRequirementMatchRaw)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsCapabilities"));
	for(const auto & world : {historicalCapabilityRulesV4(), historicalCapabilityRulesV5(), current})
		for(const auto & [key, definition] : current["classProfiles"].Struct())
		{
			SCOPED_TRACE(key);
			const auto raw = newHorizonsHeroes::resolveCapabilityRules(world, HeroClassID(HeroClassID::decode(key)));
			const nativeRules::CompiledCapabilityRules typed(raw);
			const auto * leadership = typed.leadership();
			ASSERT_NE(leadership, nullptr);
			EXPECT_EQ(leadership->usesEfficiency(), newHorizonsHeroes::usesLeadershipEfficiency(raw));
			for(const int level : {1, 2, 37, 1000000})
				EXPECT_EQ(leadership->rating(level), newHorizonsHeroes::capabilityLeadershipRating(raw, level));
			EXPECT_EQ(leadership->growthPerLevel(), newHorizonsHeroes::capabilityLeadershipPerLevel(raw));
			LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
			{
				EXPECT_EQ(leadership->requirement(creature->getId()),
					newHorizonsHeroes::capabilityCreatureLeadershipRequirement(raw, creature->getId()));
			});
			EXPECT_EQ(leadership->requirement(CreatureID::NONE), 0);
			if(leadership->usesEfficiency())
			{
				for(int rank = 0; rank < 4; ++rank)
					EXPECT_EQ(leadership->authorityRankBonus(rank), raw["leadership"]["efficiencyPolicy"]["authorityRankBonusPercent"][rank].Integer());
				for(const auto & [perk, value] : raw["leadership"]["efficiencyPolicy"]["authorityPerkBonusPercent"].Struct())
					EXPECT_EQ(leadership->authorityPerkBonus(perk), value.Integer());
			}
		}
}

TEST_F(CompiledLeadershipRules, PerkCaptureDiscountsPreserveIndependentRankAndLordRounding)
{
	const JsonNode raw(JsonPath::builtin("config/newHorizonsPerks"));
	const auto owner = nativeRules::CapturedPerkRules::compile(raw);
	const auto policy = newHorizonsNecromancy::policy(raw);
	ASSERT_TRUE(policy);
	for(int rank = 0; rank < 4; ++rank)
		for(const bool lord : {false, true})
		{
			const int expected = lord ? policy->lordOfTheDeadLeadershipReductionPercent : policy->skeletonLeadershipReductionPercent[rank];
			const int actual = owner->leadershipDiscounts().discountPercent(rank, lord);
			EXPECT_EQ(actual, expected);
			for(const int base : {1, 45, 77, 650})
				EXPECT_EQ(newHorizonsNecromancy::discountedLeadershipRequirement(base, actual),
					newHorizonsNecromancy::discountedLeadershipRequirement(base, expected));
		}
	const nativeRules::CompiledLeadershipDiscounts absent{JsonNode()};
	EXPECT_EQ(absent.discountPercent(3, true), 0);
}

TEST_F(CompiledLeadershipRules, V1AggregateAndV2V3V6SlotProfilesRetainHistoricalArithmetic)
{
	JsonNode first;
	first["schemaVersion"].Integer() = 1;
	first["rulesetVersion"].Integer() = 1;
	first["profile"]["base"].Integer() = 2000;
	first["profile"]["perLevel"].Integer() = 200;
	for(int value : {0, 10, 20, 30}) first["leadership"]["skillBonusPercent"].Vector().emplace_back(value);
	first["leadership"]["minimumMovementPercent"].Integer() = 50;
	for(int value : {1, 2, 3, 4}) first["siege"]["ballistaDamageMultiplier"].Vector().emplace_back(value);
	const nativeRules::CompiledCapabilityRules legacy(first);
	for(int level : {1, 2, 50})
		for(int rank = 0; rank < 4; ++rank)
			for(uint64_t used : {0u, 2000u, 9000u})
			{
				const auto raw = newHorizonsHeroes::capabilityLeadership(first, level, rank, used);
				const auto typed = legacy.leadership()->aggregate(level, rank, used);
				EXPECT_EQ(typed.capacity, raw.capacity);
				EXPECT_EQ(typed.used, raw.used);
				EXPECT_EQ(typed.movementPercent, raw.movementPercent);
			}
	for(const int version : {2, 3, 6})
	{
		auto world = historicalCapabilityRulesV4();
		world["rulesetVersion"].Integer() = version;
		if(version < 4)
		{
			world.Struct().erase("warMachineShop");
			world.Struct().erase("skeletonTransformer");
		}
		if(version == 2)
		{
			world["siege"].Struct().erase("siegeRating");
			world["siege"].Struct().erase("outputs");
			world["siege"].Struct().erase("directControlChance");
		}
		const auto raw = newHorizonsHeroes::resolveCapabilityRules(world, HeroClassID(HeroClassID::decode("core:knight")));
		const nativeRules::CompiledCapabilityRules compiled(raw);
		for(int level : {1, 2, 50}) EXPECT_EQ(compiled.leadership()->rating(level), newHorizonsHeroes::capabilityLeadershipRating(raw, level));
		EXPECT_EQ(compiled.leadership()->requirement(CreatureID::SKELETON), newHorizonsHeroes::capabilityCreatureLeadershipRequirement(raw, CreatureID::SKELETON));
	}
}
