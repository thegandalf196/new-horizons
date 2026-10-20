/*
 * NewHorizonsSkillSpecialtyQueryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsRuleValidation.h"
#include <array>
#include <chrono>
#include <set>

using namespace newHorizonsHeroes;
namespace
{
using detail::fields;
using detail::integer;
using detail::require;
using detail::resolve;

void legacyValidateSkillSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "coreBonusPercent", "skills", "navigationStartReplacements"});
	if(rules.Struct().contains("navigationStartReplacements"))
		require(rules["navigationStartReplacements"].isBool(), "navigation starting replacement Boolean");
	require(integer(rules["version"], 1, 1), "skill specialty version");
	require(integer(rules["coreBonusPercent"], 20, 20), "skill specialty core bonus percentage");
	const auto & skills = rules["skills"];
	require(skills.isVector() && !skills.Vector().empty() && skills.Vector().size() <= 5,
		"version 1 skill specialties must list one to five supported core skills");
	std::set<int> seen;
	for(const auto & skill : skills.Vector())
	{
		require(skill.isString()
			&& (skill.String() == "core:logistics" || skill.String() == "core:armorer"
				|| skill.String() == "core:offence" || skill.String() == "core:archery"
				|| skill.String() == "core:estates"),
			"version 1 skill specialties support only core:logistics, core:armorer, core:offence, core:archery and core:estates");
		const int skillId = resolve(SecondarySkill::entityType(), skill.String());
		require(skillId == SecondarySkill::LOGISTICS || skillId == SecondarySkill::ARMORER
			|| skillId == SecondarySkill::OFFENCE || skillId == SecondarySkill::ARCHERY
			|| skillId == SecondarySkill::ESTATES,
			"unknown version 1 skill specialty");
		require(seen.insert(skillId).second, "duplicate version 1 skill specialty");
	}
	if(rules.Struct().contains("navigationStartReplacements") && rules["navigationStartReplacements"].Bool())
		require(seen.contains(SecondarySkill::LOGISTICS), "Navigation successor requires the Logistics core specialty");
}

std::optional<SkillSpecialtyRules> legacyTypedRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("skillSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	legacyValidateSkillSpecialtyRules(found->second);
	SkillSpecialtyRules result;
	result.version = static_cast<int>(found->second["version"].Integer());
	result.coreBonusPercent = static_cast<int>(found->second["coreBonusPercent"].Integer());
	for(const auto & skill : found->second["skills"].Vector())
		result.skills.emplace_back(resolve(SecondarySkill::entityType(), skill.String()));
	return result;
}


JsonNode rulesWithSkills(std::initializer_list<const char *> skills)
{
	JsonNode rules;
	auto & declaration = rules["skillSpecialties"];
	declaration["version"].Integer() = 1;
	declaration["coreBonusPercent"].Integer() = 20;
	for(const auto * skill : skills)
		declaration["skills"].Vector().emplace_back(skill);
	return rules;
}

std::string failure(const std::function<void()> & call)
{
	try
	{
		call();
	}
	catch(const std::runtime_error & error)
	{
		return error.what();
	}
	ADD_FAILURE() << "Expected malformed captured rules to throw";
	return {};
}
}

TEST(NewHorizonsSkillSpecialtyQueryTest, EverySupportedSubsetMatchesTheFrozenTypedOracle)
{
	const std::array<const char *, 5> names = {"core:logistics", "core:armorer", "core:offence", "core:archery", "core:estates"};
	const std::array<SecondarySkill, 6> queries = {SecondarySkill(SecondarySkill::LOGISTICS),
		SecondarySkill(SecondarySkill::ARMORER), SecondarySkill(SecondarySkill::OFFENCE),
		SecondarySkill(SecondarySkill::ARCHERY), SecondarySkill(SecondarySkill::ESTATES),
		SecondarySkill(SecondarySkill::WISDOM)};
	for(unsigned mask = 1; mask < 32; ++mask)
	{
		auto rules = rulesWithSkills({});
		for(size_t index = 0; index < names.size(); ++index)
			if(mask & (1u << index))
				rules["skillSpecialties"]["skills"].Vector().emplace_back(names[index]);
		const auto before = rules;
		const auto legacy = legacyTypedRules(rules);
		const auto typed = skillSpecialtyRules(rules);
		ASSERT_TRUE(legacy);
		ASSERT_TRUE(typed);
		EXPECT_EQ(typed->version, legacy->version);
		EXPECT_EQ(typed->coreBonusPercent, legacy->coreBonusPercent);
		EXPECT_EQ(typed->skills, legacy->skills);
		for(const auto query : queries)
			EXPECT_EQ(skillSpecialtyCoreBonusPercent(rules, query),
				std::ranges::find(legacy->skills, query) != legacy->skills.end() ? legacy->coreBonusPercent : 0);
		EXPECT_EQ(rules, before);
	}
}

TEST(NewHorizonsSkillSpecialtyQueryTest, MalformedLateEntriesAndOrderingMatchTheFrozenOracle)
{
	const auto valid = rulesWithSkills({"core:armorer", "core:logistics"});
	std::vector<JsonNode> bad(9, valid);
	bad[0]["skillSpecialties"]["skills"].Vector().emplace_back("core:wisdom");
	bad[1]["skillSpecialties"]["skills"].Vector().emplace_back("core:armorer");
	bad[2]["skillSpecialties"]["skills"].Vector().emplace_back(9);
	bad[3]["skillSpecialties"]["version"].Integer() = 2;
	bad[3]["skillSpecialties"]["coreBonusPercent"].Integer() = 0;
	bad[4]["skillSpecialties"]["navigationStartReplacements"].Integer() = 1;
	bad[5]["skillSpecialties"]["unknown"].Bool() = true;
	bad[6]["skillSpecialties"]["skills"].Vector().clear();
	bad[7] = rulesWithSkills({"core:armorer"});
	bad[7]["skillSpecialties"]["navigationStartReplacements"].Bool() = true;
	bad[8] = rulesWithSkills({"core:logistics", "core:armorer", "core:offence", "core:archery", "core:estates", "core:armorer"});
	for(const auto & rules : bad)
	{
		const auto before = rules;
		const auto expected = failure([&] { (void)legacyTypedRules(rules); });
		EXPECT_EQ(failure([&] { (void)skillSpecialtyRules(rules); }), expected);
		for(const auto query : {SecondarySkill(SecondarySkill::ARMORER), SecondarySkill(SecondarySkill::WISDOM)})
			EXPECT_EQ(failure([&] { (void)skillSpecialtyCoreBonusPercent(rules, query); }), expected);
		EXPECT_EQ(rules, before);
	}
}

TEST(NewHorizonsSkillSpecialtyQueryTest, MutableRulesAndNavigationRemainLiveWithoutCaches)
{
	auto rules = rulesWithSkills({"core:armorer", "core:logistics"});
	const SecondarySkill armorer(SecondarySkill::ARMORER);
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(rules, armorer), 20);
	EXPECT_FALSE(usesNavigationStartReplacement(rules));
	rules["skillSpecialties"]["navigationStartReplacements"].Bool() = true;
	EXPECT_TRUE(usesNavigationStartReplacement(rules));
	rules["skillSpecialties"]["skills"].Vector().erase(rules["skillSpecialties"]["skills"].Vector().begin());
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(rules, armorer), 0);
	rules = rulesWithSkills({"core:armorer"});
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(rules, armorer), 20);
	rules.Struct().erase("skillSpecialties");
	EXPECT_FALSE(skillSpecialtyRules(rules));
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(rules, armorer), 0);
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(JsonNode(), armorer), 0);
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(JsonNode(JsonMap{}), armorer), 0);
	EXPECT_EQ(skillSpecialtyCoreBonusPercent(JsonNode(1), armorer), 0);
}

TEST(NewHorizonsSkillSpecialtyQueryTest, RepeatedScalarQueriesPreserveTheFrozenOracleChecksum)
{
	// Construct the shared input before timing: no map or save setup.
	const auto rules = rulesWithSkills({"core:logistics", "core:armorer", "core:offence", "core:archery", "core:estates"});
	const auto before = rules;
	constexpr int queriesPerPass = 10000;
	constexpr int expectedChecksum = queriesPerPass / 2 * 20;
	const std::array<SecondarySkill, 2> queries = {
		SecondarySkill(SecondarySkill::ARMORER), SecondarySkill(SecondarySkill::WISDOM)};
	const auto oldQuery = [&rules](SecondarySkill query)
	{
		const auto typed = legacyTypedRules(rules);
		return typed && std::ranges::find(typed->skills, query) != typed->skills.end()
			? typed->coreBonusPercent : 0;
	};
	const auto newQuery = [&rules](SecondarySkill query)
	{
		return skillSpecialtyCoreBonusPercent(rules, query);
	};
	const auto measure = [&queries](const auto & query)
	{
		int checksum = 0;
		const auto started = std::chrono::steady_clock::now();
		for(int index = 0; index < queriesPerPass; ++index)
			checksum += query(queries[index % queries.size()]);
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - started).count();
		return std::pair{checksum, elapsed};
	};
	// Two finite cohorts reverse order; no warmup/retry or elapsed pass threshold.
	const auto oldFirstOld = measure(oldQuery);
	const auto oldFirstNew = measure(newQuery);
	const auto newFirstNew = measure(newQuery);
	const auto newFirstOld = measure(oldQuery);
	EXPECT_EQ(oldFirstOld.first, expectedChecksum);
	EXPECT_EQ(oldFirstNew.first, expectedChecksum);
	EXPECT_EQ(newFirstNew.first, expectedChecksum);
	EXPECT_EQ(newFirstOld.first, expectedChecksum);
	EXPECT_EQ(rules, before);
	RecordProperty("benchmark_contract", "skill-specialty-scalar-v1-10000-alternating");
	RecordProperty("queries_per_pass", queriesPerPass);
	RecordProperty("positive_queries_per_pass", queriesPerPass / 2);
	RecordProperty("semantic_checksum", expectedChecksum);
	RecordProperty("old_first_old_us", std::to_string(oldFirstOld.second));
	RecordProperty("old_first_new_us", std::to_string(oldFirstNew.second));
	RecordProperty("new_first_new_us", std::to_string(newFirstNew.second));
	RecordProperty("new_first_old_us", std::to_string(newFirstOld.second));
}
