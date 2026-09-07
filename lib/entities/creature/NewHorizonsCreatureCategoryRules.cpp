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
	fields(rules, {"schemaVersion", "rulesetVersion", "sourceRulesetId", "categories", "creatures"});
	require(version(rules["schemaVersion"], 1), "schemaVersion");
	require(version(rules["rulesetVersion"], CREATURE_CATEGORY_RULESET_VERSION), "rulesetVersion");
	const auto source = text(rules["sourceRulesetId"]);
	require(qualified(source) && source.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ":"), "source ruleset identity");
	fields(rules["categories"], {"core", "elite", "champion"});
	for(size_t index = 0; index < categoryNames.size(); ++index)
	{
		const auto & definition = rules["categories"][std::string(categoryNames[index])];
		fields(definition, {"nameTextId", "descriptionTextId"});
		definitions[index] = {static_cast<CreatureCategory>(index), text(definition["nameTextId"]),
			text(definition["descriptionTextId"]), source, CREATURE_CATEGORY_RULESET_VERSION};
	}
	require(rules["creatures"].isStruct() && !rules["creatures"].Struct().empty(), "explicit creature assignments required");
	for(const auto & [key, value] : rules["creatures"].Struct())
	{
		require(qualified(key), "qualified creature identifier required");
		assignments.emplace(key, category(value));
	}
}

std::optional<CreatureCategoryView> CreatureCategoryRules::lookup(std::string_view scopedCreatureKey) const
{
	const auto found = assignments.find(scopedCreatureKey);
	if(found == assignments.end())
		return std::nullopt;
	return definitions.at(static_cast<size_t>(found->second));
}
}
