/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/nativeRules/CapturedRuleRef.h"
#include "../../lib/nativeRules/CompiledDevelopmentRules.h"
#include "../../lib/entities/hero/NewHorizonsPrimaryGrowth.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <algorithm>
#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"

TEST(CompiledDevelopmentRules, CurrentClassesPreserveProfilesOrderedGrowthAndLiveRanks)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const auto & [key, definition] : world["classProfiles"].Struct())
	{
		SCOPED_TRACE(key);
		const auto raw = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode(key)));
		const nativeRules::CompiledDevelopmentRules typed(raw);
		ASSERT_TRUE(typed.profile());
		const auto profile = newHorizonsHeroes::parsePrimaryProfile(raw["profile"]);
		EXPECT_EQ(typed.profile()->starting, profile.starting);
		EXPECT_EQ(typed.profile()->growth, profile.growth);
		EXPECT_EQ(typed.profile()->progressionVersion, profile.progressionVersion);
		EXPECT_EQ(typed.powerDivisor(), raw["powerDivisor"].Integer());
		EXPECT_EQ(typed.maximumPrimary(), raw["maxPrimary"].Integer());
		for(const int rank : {0, 3, 1, 2, 0})
		{
			const auto ranks = [rank](SecondarySkill) { return rank; };
			const auto expected = newHorizonsHeroes::skillGrowthChances(raw, ranks);
			const auto actual = typed.skillGrowthChances(ranks);
			ASSERT_EQ(actual.size(), expected.size());
			std::vector<newHorizonsHeroes::ExtraPrimaryRoll> a, b;
			std::vector<int> draws;
			for(size_t i = 0; i < actual.size(); ++i)
			{
				EXPECT_EQ(actual[i].skill, expected[i].skill);
				EXPECT_EQ(actual[i].attribute, expected[i].attribute);
				EXPECT_EQ(actual[i].chancePercent, expected[i].chancePercent);
				a.push_back({actual[i].attribute, actual[i].chancePercent});
				b.push_back({expected[i].attribute, expected[i].chancePercent});
				draws.push_back(static_cast<int>((i * 37 + 19) % 100));
			}
			EXPECT_EQ(newHorizonsHeroes::calculatePrimaryGrowth(*typed.profile(), a, draws),
				newHorizonsHeroes::calculatePrimaryGrowth(profile, b, draws));
		}
		EXPECT_THROW(typed.skillGrowthChances([](SecondarySkill) { return 4; }), std::runtime_error);
		for(const auto & [skill, value] : raw["skillOfferWeights"].Struct())
			EXPECT_EQ(typed.skillOfferWeight(SecondarySkill(SecondarySkill::decode(skill))),
				newHorizonsHeroes::skillOfferWeight(raw, SecondarySkill(SecondarySkill::decode(skill))));
	}
}

TEST(CompiledDevelopmentRules, MissingHistoricalPoliciesStayAbsentAndImmutableOwnersIgnoreChangedDefaults)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const nativeRules::CompiledDevelopmentRules absent{JsonNode()};
	EXPECT_FALSE(absent.active());
	EXPECT_FALSE(absent.profile());
	EXPECT_FALSE(absent.druid);
	EXPECT_FALSE(absent.skillOfferWeight(SecondarySkill::WISDOM));
	EXPECT_TRUE(absent.skillGrowthChances([](SecondarySkill) { return 3; }).empty());
	JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto knight = HeroClassID(HeroClassID::decode("core:knight"));
	auto raw = newHorizonsHeroes::resolveHeroRules(world, knight);
	nativeRules::CapturedRulePool pool;
	const auto first = pool.intern(nativeRules::CapturedRuleKind::ResolvedDevelopment, raw);
	const auto second = pool.intern(nativeRules::CapturedRuleKind::ResolvedDevelopment, raw);
	EXPECT_EQ(first, second);
	const auto owner = pool.document(first, nativeRules::CapturedRuleKind::ResolvedDevelopment);
	const auto * typed = owner->development().get();
	ASSERT_NE(typed, nullptr);
	const auto oldDivisor = typed->powerDivisor();
	raw["powerDivisor"].Integer() = oldDivisor + 1;
	const auto changed = pool.intern(nativeRules::CapturedRuleKind::ResolvedDevelopment, raw);
	EXPECT_NE(changed, first);
	EXPECT_EQ(typed->powerDivisor(), oldDivisor);
	EXPECT_EQ(pool.document(changed, nativeRules::CapturedRuleKind::ResolvedDevelopment)->development()->powerDivisor(), oldDivisor + 1);
	EXPECT_EQ(owner->development().get(), typed); // repeated probes reuse the same immutable projection
	raw["extraGrowth"].Vector().front()["chances"][3].Integer() = 101;
	EXPECT_THROW(pool.intern(nativeRules::CapturedRuleKind::ResolvedDevelopment, raw), std::runtime_error);
	EXPECT_EQ(owner->development().get(), typed);
}

TEST(CompiledDevelopmentRules, HistoricalGrowthVersionsRetainNoExtraDrawsAndTheirOwnRounding)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto initial = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode("core:knight")));
	for(const int version : {1, 2, 3})
	{
		JsonNode raw = initial;
		auto & profile = raw["profile"];
		profile["progressionVersion"].Integer() = version;
		const std::array<int, 4> growth = version == 1 ? std::array<int, 4>{3, 3, 2, 2}
			: version == 2 ? std::array<int, 4>{5, 5, 4, 4} : std::array<int, 4>{6, 6, 4, 4};
		for(size_t i = 0; i < growth.size(); ++i)
		{
			profile["growth"][i].Integer() = growth[i];
			profile["starting"][i].Integer() = version == 3 ? 5 * growth[i] : 25;
		}
		const nativeRules::CompiledDevelopmentRules typed(raw);
		const auto expected = newHorizonsHeroes::parsePrimaryProfile(profile);
		for(const int level : {1, 2, 7, 200}) EXPECT_EQ(typed.profile()->baseAtLevel(level), expected.baseAtLevel(level));
		const auto opportunities = typed.skillGrowthChances([](SecondarySkill) { return 3; });
		if(version < 3) EXPECT_TRUE(opportunities.empty());
		EXPECT_EQ(opportunities.size(), newHorizonsHeroes::skillGrowthChances(raw, [](SecondarySkill) { return 3; }).size());
	}
}

TEST(CompiledDevelopmentRules, SpecialtyScalarsAndResolvedHeroRowsMatchRawWithoutInventedPolicies)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	auto raw = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode("core:knight")));
	const nativeRules::CompiledDevelopmentRules typed(raw);
	const auto & ordinary = raw["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto & [key, value] : ordinary.Struct())
	{
		EXPECT_TRUE(typed.hasOrdinaryField(key));
		if(value.isNumber()) EXPECT_EQ(typed.ordinaryValue(key), value.Integer());
	}
	for(const auto & row : newHorizonsHeroes::detail::ordinaryCreatureSpecialtyRegistry())
	{
		const HeroTypeID id(HeroTypeID::decode(row.hero));
		ASSERT_TRUE(id.hasValue());
		const auto * admitted = typed.ordinary(id);
		EXPECT_EQ(admitted != nullptr, ordinary.Struct().contains(row.coefficient));
		if(admitted) EXPECT_EQ(admitted, &row);
	}
	for(size_t i = 0; i < typed.schoolSpellcraft.size(); ++i)
	{
		const auto expected = newHorizonsHeroes::schoolSpellcraftRules(raw, newHorizonsHeroes::SchoolSpellcraftKind(i));
		ASSERT_EQ(typed.schoolSpellcraft[i].has_value(), expected.has_value());
		if(expected)
		{
			EXPECT_EQ(typed.schoolSpellcraft[i]->percentPerLevel, expected->percentPerLevel);
			EXPECT_EQ(typed.schoolSpellcraft[i]->maximumPercent, expected->maximumPercent);
		}
	}
	for(size_t i = 0; i < typed.recovery.size(); ++i)
	{
		const auto expected = newHorizonsHeroes::recoverySpecialtyRules(raw, newHorizonsHeroes::RecoverySpecialtyKind(i));
		ASSERT_EQ(typed.recovery[i].has_value(), expected.has_value());
		if(expected)
		{
			EXPECT_EQ(typed.recovery[i]->dailyPercentPerLevel, expected->dailyPercentPerLevel);
			EXPECT_EQ(typed.recovery[i]->dailyMaximumPercent, expected->dailyMaximumPercent);
			EXPECT_EQ(typed.recovery[i]->victoryPercent, expected->victoryPercent);
			EXPECT_EQ(typed.recovery[i]->lowNormalPercent, expected->lowNormalPercent);
			EXPECT_EQ(typed.recovery[i]->firstCombatPercent, expected->firstCombatPercent);
		}
	}
	for(size_t i = 0; i < typed.learning.size(); ++i)
	{
		const auto expected = newHorizonsHeroes::learningSpecialtyRules(raw, newHorizonsHeroes::LearningSpecialtyKind(i));
		ASSERT_EQ(typed.learning[i].has_value(), expected.has_value());
		if(expected)
		{
			EXPECT_EQ(typed.learning[i]->learning.percentPerLevel, expected->learning.percentPerLevel);
			EXPECT_EQ(typed.learning[i]->learning.maximumPercent, expected->learning.maximumPercent);
			EXPECT_EQ(typed.learning[i]->mentorExperiencePercent, expected->mentorExperiencePercent);
		}
	}
	// Absence and old rows do not borrow installed numerical policies.
	raw.Struct().erase("schoolSpellcraftSpecialties");
	raw.Struct().erase("recoverySpecialties");
	raw.Struct().erase("additionalSpecialties");
	const nativeRules::CompiledDevelopmentRules historical(raw);
	for(const auto & rule : historical.schoolSpellcraft) EXPECT_FALSE(rule);
	for(const auto & rule : historical.recovery) EXPECT_FALSE(rule);
	for(const auto & rule : historical.learning) EXPECT_FALSE(rule);
	EXPECT_EQ(historical.ordinary(HeroTypeID(HeroTypeID::decode("core:terek"))), nullptr);
	EXPECT_EQ(historical.ordinaryValue("relentlessBarrageDamagePercent"), 0);
}

TEST(CompiledDevelopmentRules, FactionAliasesRewardMigrationAndExclusionMatchRawForEveryRegisteredSkill)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto raw = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode("core:knight")));
	const nativeRules::CompiledDevelopmentRules typed(raw);
	for(int id = 0; id < LIBRARY->skillh->size(); ++id)
	{
		const SecondarySkill skill(id);
		EXPECT_EQ(typed.isExcludedSkill(skill), newHorizonsHeroes::isExcludedSkill(raw, skill));
		EXPECT_EQ(typed.isFactionSkill(skill), newHorizonsHeroes::isFactionSkill(raw, skill));
		EXPECT_EQ(typed.normalizeRewardSkill(skill), newHorizonsHeroes::normalizeRewardSkill(raw, skill));
		for(const auto & [key, row] : raw["startingSkills"]["factionSkills"].Struct())
		{
			const FactionID faction(FactionID::decode(key));
			EXPECT_EQ(typed.factionSkill(faction), newHorizonsHeroes::factionSkill(raw, faction));
			EXPECT_EQ(typed.isFactionSkillForFaction(faction, skill), newHorizonsHeroes::isFactionSkillForFaction(raw, faction, skill));
		}
	}
	EXPECT_FALSE(typed.normalizeRewardSkill(SecondarySkill::NONE));
}

TEST(CompiledDevelopmentRules, PreparedCreaturePrototypesShareOnlyImmutableDefinitionsAndAttachIndependentBonuses)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto raw = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode("core:knight")));
	const nativeRules::CompiledDevelopmentRules typed(raw);
	ASSERT_EQ(raw["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"].Struct().size(), 3u);
	for(const auto & [key, row] : raw["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"].Struct())
	{
		const HeroTypeID hero(HeroTypeID::decode(key));
		const auto expected = nativeRules::preparePilotCreature(raw, hero);
		const auto actual = typed.preparedCreature(hero);
		ASSERT_NE(actual, nullptr);
		ASSERT_NE(expected, nullptr);
		EXPECT_EQ(actual->bonus().toJsonNode(), expected->bonus().toJsonNode());
		auto first = actual->attach();
		auto second = actual->attach();
		EXPECT_NE(first.get(), second.get());
		EXPECT_EQ(first->toJsonNode(), expected->attach()->toJsonNode());
		EXPECT_EQ(first->source, expected->bonus().source);
		EXPECT_EQ(first->sid, expected->bonus().sid);
		const int original = second->val;
		first->val += 1;
		EXPECT_EQ(second->val, original);
		EXPECT_EQ(actual->bonus().val, original);
	}
}
