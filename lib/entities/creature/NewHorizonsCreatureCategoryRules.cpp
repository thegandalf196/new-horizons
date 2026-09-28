/*
 * NewHorizonsCreatureCategoryRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsCreatureCategoryRules.h"
#include "../../constants/StringConstants.h"

#include <cmath>
#include <stdexcept>

namespace newHorizonsCreatures
{
namespace
{
constexpr std::array<std::string_view, 3> categoryNames = {"core", "elite", "champion"};

void require(bool condition, const std::string & detail)
{
	if(!condition)
		throw std::runtime_error("Invalid New Horizons creature categories: " + detail);
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

bool version(const JsonNode & node, int expected)
{
	return node.isNumber() && std::isfinite(node.Float()) && node.Float() == expected;
}

int parseRulesetVersion(const JsonNode & node)
{
	require(node.isNumber() && std::isfinite(node.Float()) && std::floor(node.Float()) == node.Float(), "integer rulesetVersion required");
	const int result = node.Integer();
	require(result >= 1 && result <= CREATURE_CATEGORY_RULESET_VERSION, "unsupported rulesetVersion");
	return result;
}

bool qualified(const std::string & name)
{
	const auto separator = name.find(':');
	return separator != std::string::npos && separator > 0 && separator + 1 < name.size()
		&& name.find(':', separator + 1) == std::string::npos;
}

std::string text(const JsonNode & node)
{
	require(node.isString() && !node.String().empty(), "nonempty text identifier required");
	return node.String();
}

CreatureCategory category(const JsonNode & node)
{
	require(node.isString(), "named category required, not a numerical tier");
	const auto found = std::find(categoryNames.begin(), categoryNames.end(), node.String());
	require(found != categoryNames.end(), "unknown category " + node.String());
	return static_cast<CreatureCategory>(found - categoryNames.begin());
}
}

CreatureCategoryRules::CreatureCategoryRules(const JsonNode & snapshot)
	: rules(snapshot)
{
	if(rules.isNull() || (rules.isStruct() && rules.Struct().empty()))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "sourceRulesetId", "categories", "creatures", "growthLines"});
	require(version(rules["schemaVersion"], 1), "schemaVersion");
	rulesetVersion = parseRulesetVersion(rules["rulesetVersion"]);
	const auto source = text(rules["sourceRulesetId"]);
	require(qualified(source) && source.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ":"), "source ruleset identity");
	fields(rules["categories"], {"core", "elite", "champion"});
	for(size_t index = 0; index < categoryNames.size(); ++index)
	{
		const auto & definition = rules["categories"][std::string(categoryNames[index])];
		fields(definition, {"nameTextId", "descriptionTextId"});
		definitions[index] = {static_cast<CreatureCategory>(index), text(definition["nameTextId"]),
			text(definition["descriptionTextId"]), source, rulesetVersion};
	}
	require(rules["creatures"].isStruct() && !rules["creatures"].Struct().empty(), "explicit creature assignments required");
	for(const auto & [key, value] : rules["creatures"].Struct())
	{
		require(qualified(key), "qualified creature identifier required");
		assignments.emplace(key, category(value));
	}

	if(rulesetVersion == 1)
	{
		require(rules["growthLines"].isNull(), "growthLines require creature ruleset version 2");
		return;
	}

	const auto & lines = rules["growthLines"];
	require(lines.isStruct() && !lines.Struct().empty(), "explicit growth lines required by creature ruleset version 2");
	for(const auto & [baseCreature, definition] : lines.Struct())
	{
		require(qualified(baseCreature), "qualified base creature identifier required for growth line");
		fields(definition, {"weeklyBaseGrowth", "hordeGrowthOverride", "members"});
		const auto & growth = definition["weeklyBaseGrowth"];
		require(growth.isNumber() && std::isfinite(growth.Float()) && std::floor(growth.Float()) == growth.Float()
			&& growth.Integer() > 0 && growth.Integer() <= 1000000, "positive integer weeklyBaseGrowth required");
		require(definition["members"].isVector() && !definition["members"].Vector().empty(), "nonempty growth-line members required");

		CreatureGrowthLine line;
		line.weeklyBaseGrowth = growth.Integer();
		if(!definition["hordeGrowthOverride"].isNull())
		{
			const auto & hordeGrowth = definition["hordeGrowthOverride"];
			require(hordeGrowth.isNumber() && std::isfinite(hordeGrowth.Float()) && std::floor(hordeGrowth.Float()) == hordeGrowth.Float()
				&& hordeGrowth.Integer() >= 0 && hordeGrowth.Integer() <= 1000000, "nonnegative integer hordeGrowthOverride required");
			line.hordeGrowthOverride = hordeGrowth.Integer();
		}
		std::set<std::string> uniqueMembers;
		bool containsBaseCreature = false;
		for(const auto & member : definition["members"].Vector())
		{
			const auto memberKey = text(member);
			require(qualified(memberKey), "qualified growth-line member identifier required");
			require(uniqueMembers.insert(memberKey).second, "duplicate member in growth line " + baseCreature);
			require(weeklyBaseGrowthByCreature.emplace(memberKey, line.weeklyBaseGrowth).second,
				"creature belongs to more than one growth line: " + memberKey);
			if(line.hordeGrowthOverride)
				hordeGrowthOverridesByCreature.emplace(memberKey, *line.hordeGrowthOverride);
			containsBaseCreature |= memberKey == baseCreature;
			line.members.push_back(memberKey);
		}
		require(containsBaseCreature, "growth line must include its base creature: " + baseCreature);
		growthLines.emplace(baseCreature, std::move(line));
	}
}

std::optional<CreatureCategoryView> CreatureCategoryRules::lookup(std::string_view scopedCreatureKey) const
{
	const auto found = assignments.find(scopedCreatureKey);
	if(found == assignments.end())
		return std::nullopt;
	return definitions.at(static_cast<size_t>(found->second));
}

std::optional<int> CreatureCategoryRules::weeklyBaseGrowth(std::string_view scopedCreatureKey) const
{
	const auto found = weeklyBaseGrowthByCreature.find(scopedCreatureKey);
	if(found == weeklyBaseGrowthByCreature.end())
		return std::nullopt;
	return found->second;
}

std::optional<int> CreatureCategoryRules::hordeGrowthOverride(std::string_view scopedCreatureKey) const
{
	const auto found = hordeGrowthOverridesByCreature.find(scopedCreatureKey);
	if(found == hordeGrowthOverridesByCreature.end())
		return std::nullopt;
	return found->second;
}
}
