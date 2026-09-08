#pragma once
#include "../../lib/entities/hero/NewHorizonsMasteryState.h"

inline JsonNode logisticsMasteryRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMasteries"));
	rules["rulesetVersion"].Integer() = 2;
	const std::array<std::string, 3> ids{"logisticsForcedMarch", "logisticsQuartermaster", "logisticsPathfinder"};
	const std::array<std::string, 3> effects{"forcedMarch", "quartermaster", "pathfinder"};
	const std::array<int, 3> values{300, 1, 50};
	auto & options = rules["skills"]["core:logistics"]["options"].Vector();
	options.clear();
	for(size_t i = 0; i < ids.size(); ++i)
	{
		JsonNode option;
		option["id"].String() = "new-horizons:" + ids[i];
		option["effect"].String() = effects[i];
		option["magnitude"].Integer() = values[i];
		option["nameTextId"].String() = "new-horizons.mastery." + ids[i] + ".name";
		option["descriptionTextId"].String() = "new-horizons.mastery." + ids[i] + ".description";
		option["iconKey"].String() = "NH_mastery_" + ids[i];
		options.push_back(std::move(option));
	}
	return rules;
}
