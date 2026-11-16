/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapping/CMap.h"
#include <array>
#include <string_view>

namespace estatesTest
{
// Data-only capture of the pre-revision Estates catalogue at 1aaa7d24b6.
// Keep this independent of the installed current Estates roster and presentation.
inline constexpr std::string_view LEGACY_ESTATES_SKILL = R"estates(
{
  "id": "new-horizons:estates",
  "name": "Estates",
  "domain": "Strategic",
  "availability": "All heroes",
  "description": "Increase income from Gold, mines, markets and towns.",
  "ranks": {
    "basic": {
      "description": "The kingdom receives +125 Gold per day while the hero is active.",
      "effect": {
        "status": "active",
        "description": "The kingdom receives +125 Gold per day while the hero is active."
      }
    },
    "advanced": {
      "description": "Daily income from Estates increases to +250 Gold.",
      "effect": {
        "status": "active",
        "description": "Daily income from Estates increases to +250 Gold."
      }
    },
    "expert": {
      "description": "Daily income from Estates increases to +500 Gold.",
      "effect": {
        "status": "active",
        "description": "Daily income from Estates increases to +500 Gold."
      }
    }
  },
  "perks": [
    {
      "id": "new-horizons:estates.taxCollector",
      "name": "Tax Collector",
      "requires": "basic",
      "description": "Gain 100 Gold daily per owned town, up to 500 Gold.",
      "effect": {
        "status": "active",
        "description": "Gain 100 Gold daily per owned town, up to 500 Gold."
      },
      "details": "Each active holder grants 100 Gold per day for each town owned by that hero’s player, capped at 500 Gold per holder per day. This is separate from the Estates core income."
    },
    {
      "id": "new-horizons:estates.landSurveyor",
      "name": "Land Surveyor",
      "requires": "basic",
      "description": "The first mine captured by this hero each week immediately produces three days of normal output.",
      "effect": {
        "status": "active",
        "description": "The first mine captured by this hero each week immediately produces three days of normal output."
      },
      "details": "Once each week, the first mine captured by this hero immediately grants three times its normal daily resource output. Merely visiting an already owned mine does not qualify; use this hero’s capture receipt, not a kingdom-wide mine-visit count."
    },
    {
      "id": "new-horizons:estates.prosperity",
      "name": "Prosperity",
      "requires": "basic",
      "description": "While active, this hero grants 1 Wood and 1 Ore daily.",
      "effect": {
        "status": "active",
        "description": "While active, this hero grants 1 Wood and 1 Ore daily."
      },
      "details": "Each active owned hero with Prosperity independently grants 1 Wood and 1 Ore per day through ordinary hero resource income. Garrisoned owned heroes count as active; tavern, pool, dead and non-owned heroes do not."
    },
    {
      "id": "new-horizons:estates.merchantPrince",
      "name": "Merchant Prince",
      "requires": "basic",
      "description": "A town where this hero resides trades as though you owned two extra Marketplaces.",
      "effect": {
        "status": "active",
        "description": "A town where this hero resides trades as though you owned two extra Marketplaces."
      },
      "details": "While this hero visits or garrisons an owned town with a Marketplace, that town’s exchange rates count two additional Marketplaces. Apply this once per town even when both residents own the perk; preserve ordinary Marketplace caps and ownership rules."
    },
    {
      "id": "new-horizons:estates.stewardship",
      "name": "Stewardship",
      "requires": "advanced",
      "description": "While stationed in an owned town, this hero grants 250 extra Gold daily.",
      "effect": {
        "status": "active",
        "description": "While stationed in an owned town, this hero grants 250 extra Gold daily."
      },
      "details": "Each qualifying visiting or garrison hero stationed in an owned town grants that town 250 additional Gold per day. Two distinct qualifying residents each contribute; this does not scale with Lord Haart’s Estates core specialty."
    },
    {
      "id": "new-horizons:estates.conscription",
      "name": "Conscription",
      "requires": "advanced",
      "description": "Creatures recruited directly into this hero’s army cost 10% fewer resources.",
      "effect": {
        "status": "active",
        "description": "Creatures recruited directly into this hero’s army cost 10% fewer resources."
      },
      "details": "Apply the discount only when this hero is the actual recruitment recipient, including field dwelling and War Machine purchases. Recruitment into a town’s upper army uses its garrison hero; a town without a garrison hero receives no discount, even if a visiting hero owns Conscription. For each resource, charge ceil(undiscounted unit price × recruited count × 90 / 100), preserving zero costs. Ten units costing 1 Gold each therefore cost 9 Gold, rather than rounding each unit separately. Preserve normal recruitment availability, free sources, ownership and legal Command placement."
    },
    {
      "id": "new-horizons:estates.architect",
      "name": "Architect",
      "requires": "advanced",
      "description": "Buildings in owned towns where this hero resides cost 20% less Gold, Wood and Ore.",
      "effect": {
        "status": "active",
        "description": "Buildings in owned towns where this hero resides cost 20% less Gold, Wood and Ore."
      },
      "details": "A qualifying visiting or garrison hero grants the owned town one 20% building discount. Charge ceil(ordinary cost × 80 / 100) separately for Gold, Wood and Ore; preserve zero costs and every precious-resource cost unchanged. Two holders do not double the discount. Keep ordinary ownership, building prerequisites and daily construction limits; displayed affordability and actual payment use the same price."
    },
    {
      "id": "new-horizons:estates.mercenaryCompany",
      "name": "Mercenary Company",
      "requires": "advanced",
      "description": "Each week, every owned mine gains Rogues equal to this hero’s current level.",
      "effect": {
        "status": "active",
        "description": "Each week, every owned mine gains Rogues equal to this hero’s current level."
      },
      "details": "At the start of each week, every owned mine gains Rogues equal to each active holder’s current hero level; add qualifying holders’ contributions. Merge with existing Rogues or use a free native garrison slot. If all seven slots are occupied without Rogues, preserve undelivered guards in a pending reserve rather than overwriting troops or losing units. Keep native mine-garrison transfers and legal Command placement when a hero collects guards; this grants no additional Hero Action."
    },
    {
      "id": "new-horizons:estates.banking",
      "name": "Banking",
      "requires": "expert",
      "description": "Each week, gain 1% of your Gold treasury, up to 1,000 Gold.",
      "effect": {
        "status": "active",
        "description": "Each week, gain 1% of your Gold treasury, up to 1,000 Gold."
      },
      "details": "At the start of each week, each active holder grants floor(current Gold treasury / 100), capped at 1,000 Gold per holder. All holders use the same pre-income treasury snapshot; do not compound interest from another holder’s award or income just granted."
    },
    {
      "id": "new-horizons:estates.alchemist",
      "name": "Alchemist",
      "requires": "expert",
      "description": "While active, this hero grants 1 Mercury, Sulfur, Crystal and Gems daily.",
      "effect": {
        "status": "active",
        "description": "While active, this hero grants 1 Mercury, Sulfur, Crystal and Gems daily."
      },
      "details": "Each active owned holder independently grants 1 of each of the four precious resources daily: Mercury, Sulfur, Crystal and Gems. Garrisoned owned heroes qualify under ordinary hero income rules. Preserve independent resource specialties; this perk adds no Wood, Ore or Gold."
    }
  ],
  "details": "Estates is available to all heroes. Its active hero daily Gold income remains 125, 250 or 500 at Basic, Advanced or Expert rank. Lord Haart’s existing specialty scales only that core income. Perk income and retained hero resource specialties keep their independent rules."
}
)estates";

inline JsonNode legacyEstatesPerks()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	rules.Struct().erase("estatesPolicyVersion");
	rules.Struct().erase("fourOfferProgressionVersion");
	rules["unifiedSkillProgressionVersion"].Integer() = 1;
	rules["skills"]["new-horizons:estates"] = JsonNode(LEGACY_ESTATES_SKILL.data(),
		LEGACY_ESTATES_SKILL.size(), "legacy-estates-skill");
	// This is a complete historical capture. An ordinary merge would inherit
	// newly installed policy markers alongside the historical perk roster.
	rules.setOverrideFlag(true);
	return rules;
}

inline JsonNode legacyEstatesHeroes()
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	constexpr std::array<const char *, 7> prosperity = {"core:rissa", "core:calid", "core:sephinroth",
		"core:saurug", "core:grindan", "core:mutare", "core:mutareDrake"};
	const auto restore = [&heroes](const char * hero, const char * oldPerk)
	{
		const auto restoreRows = [oldPerk](JsonNode & rows)
		{
			for(auto & row : rows.Vector())
				if(row["skill"].String() == "new-horizons:estates")
					row["perk"].String() = oldPerk;
		};
		restoreRows(heroes["startingSkills"]["startingPerkProfiles"][hero]);
		restoreRows(heroes["startingSkills"]["startingDevelopmentProfiles"][hero]["startingPerks"]);
	};
	for(const auto * hero : prosperity)
		restore(hero, "new-horizons:estates.prosperity");
	restore("core:jenova", "new-horizons:estates.landSurveyor");
	return heroes;
}

inline void applyLegacyEstatesHeroes(CMap * map)
{
	map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, legacyEstatesHeroes());
}
}
