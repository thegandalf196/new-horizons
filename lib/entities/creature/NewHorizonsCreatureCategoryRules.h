/*
 * NewHorizonsCreatureCategoryRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../json/JsonNode.h"
#include "../../constants/EntityIdentifiers.h"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace newHorizonsCreatures
{
constexpr int CREATURE_CATEGORY_RULESET_VERSION = 1;

enum class CreatureCategory : uint8_t
{
	CORE,
	ELITE,
	CHAMPION
};

struct DLL_LINKAGE CreatureCategoryView
{
	CreatureCategory category = CreatureCategory::CORE;
	std::string nameTextId;
	std::string descriptionTextId;
	std::string sourceRulesetId;
	int rulesetVersion = 0;
	bool operator==(const CreatureCategoryView &) const = default;
};

/// Immutable captured rules. Entity validation is separate from structural parsing
/// so standalone data tests do not require an installed creature registry.
/// Captures explicit rows only: no creature tier, upgrade or leadership inference.
class DLL_LINKAGE CreatureCategoryRules
{
	JsonNode rules;
	std::array<CreatureCategoryView, 3> definitions{};
	std::map<std::string, CreatureCategory, std::less<>> assignments;
public:
	CreatureCategoryRules() = default;
	explicit CreatureCategoryRules(const JsonNode & snapshot);

	const JsonNode & getRules() const { return rules; }
	/// Unmapped keys and absent contexts return null. No installation/global or
	/// alternate-context argument exists: absent battle data cannot fall back here.
	std::optional<CreatureCategoryView> lookup(std::string_view scopedCreatureKey) const;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			h & rules;
		else
		{
			JsonNode snapshot;
			h & snapshot;
			*this = CreatureCategoryRules(snapshot);
		}
	}
};

/// Reject unknown/noncanonical entity keys. Never infer an upgrade/category.
DLL_LINKAGE void validateCreatureCategoryEntities(const CreatureCategoryRules & rules);
/// Validate the entire candidate before a world may publish any of its rows.
DLL_LINKAGE CreatureCategoryRules captureCreatureCategoryRules(const JsonNode & snapshot);
/// Invalid IDs, missing contexts and unmapped creatures all return null.
DLL_LINKAGE std::optional<CreatureCategoryView> creatureCategoryView(const CreatureCategoryRules & rules, CreatureID creature);
}
