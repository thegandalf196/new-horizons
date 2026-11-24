/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LegacyPerkPresentation.h"
#include "NewHorizonsPerkRules.h"
#include "../../json/JsonNode.h"
#include <array>
#include <optional>
#include <stdexcept>
namespace newHorizonsHeroes
{
void normalizeLegacyHeroProcText(JsonNode & rules)
{
	if(!rules.isStruct() || !usesPerkRules(rules)
		|| !static_cast<const JsonNode &>(rules)["skills"].isStruct())
		return;
	struct LegacyText
	{
		const char * skill;
		const char * perk;
		const char * name;
		const char * description;
	};
	const std::array<LegacyText, 4> legacy = {{
		{"new-horizons:luck", "new-horizons:luck.luckyRecovery", "Lucky Recovery",
			"When a melee attack triggers positive Luck, surviving creatures in the attacker recover HP equal to 10% of actual damage dealt."},
		{"new-horizons:luck", "new-horizons:luck.gambler", "Gambler",
			"The first friendly attack each round gains +3 Luck for that attack. If positive Luck does not trigger, the attacking stack suffers -2 Luck until its next activation."},
		{"new-horizons:luck", "new-horizons:luck.twistOfFate", "Twist of Fate",
			"The first random combat roll each combat that produces a negative result for the hero's army is automatically rerolled once. Deterministic effects cannot be rerolled."},
		{"new-horizons:discipline", "new-horizons:discipline.heroicSpirit", "Heroic Spirit",
			"When positive Morale triggers, the affected stack gains one additional retaliation until its next activation."}
	}};
	// Resource services are initialized before world/crossover loading. Load the
	// shipped text only when a recognized legacy row is present, never cache a
	// second catalogue or replace the saved gameplay registry/provenance.
	// Warcasting 2 (Arcane Inspiration) is displayed as Spell-Forge since the
	// separate Warcasting Skill exists. Only the exact historical strings change.
	{
		auto & skills = rules["skills"].Struct();
		const auto skill = skills.find("new-horizons:warcasting");
		if(skill != skills.end() && static_cast<const JsonNode &>(skill->second).isStruct())
		{
			auto & name = skill->second["name"];
			if(name.isString() && name.String() == "Warcasting")
				name.String() = "Spell-Forge";
			if(static_cast<const JsonNode &>(skill->second)["perks"].isVector())
				for(auto & perk : skill->second["perks"].Vector())
				{
					const auto & view = static_cast<const JsonNode &>(perk);
					if(!view.isStruct() || !view["details"].isString())
						continue;
					auto & details = perk["details"].String();
					constexpr std::string_view oldText = "At Basic, Advanced and Expert Warcasting this totals";
					constexpr std::string_view newText = "At Basic, Advanced and Expert Spell-Forge this totals";
					if(const auto at = details.find(oldText); at != std::string::npos)
						details.replace(at, oldText.size(), newText);
				}
		}
	}
	std::optional<JsonNode> canonical;
	for(const auto & entry : legacy)
	{
		auto & skills = rules["skills"].Struct();
		const auto skill = skills.find(entry.skill);
		if(skill == skills.end())
			continue;
		const auto & skillView = static_cast<const JsonNode &>(skill->second);
		if(!skillView.isStruct() || !skillView["perks"].isVector())
			continue;
		for(auto & perk : skill->second["perks"].Vector())
		{
			const auto & view = static_cast<const JsonNode &>(perk);
			if(!view.isStruct() || !view["id"].isString() || !view["name"].isString()
				|| !view["description"].isString() || !view["effect"].isStruct()
				|| !view["effect"]["description"].isString()
				|| view["id"].String() != entry.perk || view["name"].String() != entry.name
				|| view["description"].String() != entry.description
				|| view["effect"]["description"].String() != entry.description)
				continue;
			if(!canonical)
				canonical.emplace(JsonPath::builtin("config/newHorizonsPerks"));
			const auto current = perkDefinition(*canonical, entry.skill, entry.perk);
			if(!current)
				throw std::runtime_error("Missing canonical Hero proc perk text");
			perk["name"].String() = current->name;
			perk["description"].String() = current->description;
			perk["effect"]["description"].String() = current->effect["description"].String();
		}
	}
}
}
