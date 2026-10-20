/*
 * NewHorizonsSkillSpecialtyRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSkillSpecialtyRules.h"
#include "NewHorizonsRuleValidation.h"
#include <array>
#include <set>

namespace newHorizonsHeroes
{
namespace
{
using detail::fields;
using detail::integer;
using detail::require;
using detail::resolve;

struct ValidatedSkillSpecialties
{
	std::array<int, 5> skills{};
	size_t count = 0;
};

ValidatedSkillSpecialties validateSkillSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "coreBonusPercent", "skills", "navigationStartReplacements"});
	if(rules.Struct().contains("navigationStartReplacements"))
		require(rules["navigationStartReplacements"].isBool(), "navigation starting replacement Boolean");
	require(integer(rules["version"], 1, 1), "skill specialty version");
	require(integer(rules["coreBonusPercent"], 20, 20), "skill specialty core bonus percentage");
	const auto & skills = rules["skills"];
	require(skills.isVector() && !skills.Vector().empty() && skills.Vector().size() <= 5,
		"version 1 skill specialties must list one to five supported core skills");
	ValidatedSkillSpecialties result;
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
		result.skills[result.count++] = skillId;
	}
	if(rules.Struct().contains("navigationStartReplacements") && rules["navigationStartReplacements"].Bool())
		require(seen.contains(SecondarySkill::LOGISTICS), "Navigation successor requires the Logistics core specialty");
	return result;
}

}

namespace detail
{
void validateOptionalSkillSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	const auto found = rules.Struct().find("skillSpecialties");
	if(found != rules.Struct().end())
		validateSkillSpecialtyRules(found->second);
}

}

bool usesNavigationStartReplacement(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules["skillSpecialties"].isStruct())
		return false;
	const auto & specialties = rules["skillSpecialties"];
	validateSkillSpecialtyRules(specialties);
	return specialties.Struct().contains("navigationStartReplacements")
		&& specialties["navigationStartReplacements"].Bool();
}

std::optional<SkillSpecialtyRules> skillSpecialtyRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("skillSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	const auto validated = validateSkillSpecialtyRules(found->second);
	SkillSpecialtyRules result;
	result.version = static_cast<int>(found->second["version"].Integer());
	result.coreBonusPercent = static_cast<int>(found->second["coreBonusPercent"].Integer());
	for(size_t index = 0; index < validated.count; ++index)
		result.skills.emplace_back(validated.skills[index]);
	return result;
}

int skillSpecialtyCoreBonusPercent(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return 0;
	const auto found = resolvedRules.Struct().find("skillSpecialties");
	if(found == resolvedRules.Struct().end())
		return 0;
	// Validate every entry before answering, including entries after a match.
	const auto validated = validateSkillSpecialtyRules(found->second);
	const auto end = validated.skills.begin() + validated.count;
	if(std::find(validated.skills.begin(), end, skill.getNum()) == end)
		return 0;
	return static_cast<int>(found->second["coreBonusPercent"].Integer());
}
}
