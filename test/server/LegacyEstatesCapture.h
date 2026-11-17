/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include <algorithm>
#include <stdexcept>
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

// Authored generic choices captured at1aaa7d24b6. Keep these independent of
// current skills-only profiles and catalogue ordering.
struct HistoricalGenericStart
{
	const char * hero;
	const char * skill;
	const char * perk;
};
inline constexpr std::array<HistoricalGenericStart, 163> HISTORICAL_GENERIC_STARTS{{
	{"core:sylvia", "new-horizons:logistics", "new-horizons:logistics.navigation"},
	{"core:voy", "new-horizons:logistics", "new-horizons:logistics.navigation"},
	{"core:terek", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:oris", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:saurug", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:alamar", "new-horizons:shadowMagic", "new-horizons:shadowMagic.bloodDrinker"},
	{"core:jaegar", "new-horizons:wisdom", "new-horizons:wisdom.mysticism"},
	{"core:jeddite", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:geon", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:deemer", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:sephinroth", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:straker", "new-horizons:warcasting", "new-horizons:warcasting.spellward"},
	{"core:charna", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:isra", "new-horizons:learning", "new-horizons:learning.historian"},
	{"core:septienna", "new-horizons:spellcraft", "new-horizons:spellcraft.arcaneFocus"},
	{"core:nimbus", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:thant", "new-horizons:wisdom", "new-horizons:wisdom.mysticism"},
	{"core:vidomina", "new-horizons:learning", "new-horizons:learning.scholar"},
	{"core:nagash", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:ayden", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:xyron", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:axsis", "new-horizons:wisdom", "new-horizons:wisdom.mysticism"},
	{"core:ash", "new-horizons:chaosMagic", "new-horizons:chaosMagic.frenziedCurse"},
	{"core:astral", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:serena", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:daremyth", "new-horizons:luck", "new-horizons:luck.secondChance"},
	{"core:aine", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:thorgrim", "new-horizons:warcasting", "new-horizons:warcasting.spellward"},
	{"core:coronius", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:elleshar", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:malcom", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:aeris", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:adelaide", "new-horizons:havocMagic", "new-horizons:havocMagic.cryomancer"},
	{"core:ingham", "new-horizons:wisdom", "new-horizons:wisdom.mysticism"},
	{"core:sanya", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:caitlin", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:mirlanda", "new-horizons:shadowMagic", "new-horizons:shadowMagic.witheringTouch"},
	{"core:rosic", "new-horizons:wisdom", "new-horizons:wisdom.mysticism"},
	{"core:andra", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:tiva", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:clancy", "new-horizons:logistics", "new-horizons:logistics.pathfinding"},
	{"core:piquedram", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:thane", "new-horizons:learning", "new-horizons:learning.historian"},
	{"core:torosar", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:iona", "new-horizons:learning", "new-horizons:learning.scholar"},
	{"core:fiona", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:ignatius", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:lacus", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:orrin", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:valeska", "new-horizons:archery", "new-horizons:archery.pointBlankShot"},
	{"core:edric", "new-horizons:armorer", "new-horizons:armorer.countercharge"},
	{"core:lordHaart", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:sorsha", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:christian", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:tyris", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:adela", "new-horizons:lightMagic", "new-horizons:lightMagic.benediction"},
	{"core:cuthbert", "new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"},
	{"core:loynis", "new-horizons:lightMagic", "new-horizons:lightMagic.benediction"},
	{"core:rion", "new-horizons:warMachines", "new-horizons:warMachines.surgeon"},
	{"core:mephala", "new-horizons:armorer", "new-horizons:armorer.pavise"},
	{"core:ufretin", "new-horizons:warcasting", "new-horizons:warcasting.spellward"},
	{"core:jenova", "new-horizons:estates", "new-horizons:estates.landSurveyor"},
	{"core:ryland", "new-horizons:diplomacy", "new-horizons:diplomacy.negotiator"},
	{"core:ivor", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:kyrre", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:uland", "new-horizons:lightMagic", "new-horizons:lightMagic.healer"},
	{"core:gem", "new-horizons:warMachines", "new-horizons:warMachines.surgeon"},
	{"core:melodia", "new-horizons:luck", "new-horizons:luck.fortuneSFavor"},
	{"core:alagar", "new-horizons:havocMagic", "new-horizons:havocMagic.cryomancer"},
	{"core:josephine", "new-horizons:armorer", "new-horizons:armorer.pavise"},
	{"core:neela", "new-horizons:armorer", "new-horizons:armorer.ironDiscipline"},
	{"core:fafner", "new-horizons:warcasting", "new-horizons:warcasting.spellward"},
	{"core:halon", "new-horizons:warcasting", "new-horizons:warcasting.arcaneValor"},
	{"core:rissa", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:theodorus", "new-horizons:learning", "new-horizons:learning.mentor"},
	{"core:solmyr", "new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"},
	{"core:cyra", "new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"},
	{"core:rashka", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:marius", "new-horizons:armorer", "new-horizons:armorer.ironDiscipline"},
	{"core:octavia", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:calh", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:pyre", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:nymus", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:olema", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:calid", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:xarfax", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:zydar", "new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"},
	{"core:vokial", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:moandor", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:tamika", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:clavius", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:galthran", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:aislinn", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:sandro", "new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"},
	{"core:xsi", "new-horizons:shadowMagic", "new-horizons:shadowMagic.witheringTouch"},
	{"core:lorelei", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:arlach", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:dace", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:ajit", "new-horizons:warcasting", "new-horizons:warcasting.spellward"},
	{"core:damacon", "new-horizons:estates", "new-horizons:estates.taxCollector"},
	{"core:gunnar", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:synca", "new-horizons:recruitment", "new-horizons:recruitment.drillSergeant"},
	{"core:shakti", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:malekith", "new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"},
	{"core:darkstorn", "new-horizons:wisdom", "new-horizons:wisdom.intelligence"},
	{"core:yog", "new-horizons:warMachines", "new-horizons:warMachines.precisionBombardment"},
	{"core:gurnisson", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:jabarkas", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:shiva", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:gretchin", "new-horizons:logistics", "new-horizons:logistics.pathfinding"},
	{"core:krellion", "new-horizons:discipline", "new-horizons:discipline.steadfast"},
	{"core:cragHack", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:tyraxor", "new-horizons:battlecraft", "new-horizons:battlecraft.tactics"},
	{"core:gird", "new-horizons:spellcraft", "new-horizons:spellcraft.arcaneFocus"},
	{"core:vey", "new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"},
	{"core:dessa", "new-horizons:logistics", "new-horizons:logistics.pathfinding"},
	{"core:zubin", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:gundula", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:bron", "new-horizons:armorer", "new-horizons:armorer.countercharge"},
	{"core:drakon", "new-horizons:discipline", "new-horizons:discipline.steadfast"},
	{"core:wystan", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:tazar", "new-horizons:armorer", "new-horizons:armorer.pavise"},
	{"core:alkin", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:korbac", "new-horizons:logistics", "new-horizons:logistics.pathfinding"},
	{"core:gerwulf", "new-horizons:warMachines", "new-horizons:warMachines.masterGunner"},
	{"core:broghild", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:verdish", "new-horizons:warMachines", "new-horizons:warMachines.surgeon"},
	{"core:merist", "new-horizons:natureMagic", "new-horizons:natureMagic.herbalist"},
	{"core:styg", "new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"},
	{"core:pasis", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:thunar", "new-horizons:armorer", "new-horizons:armorer.pavise"},
	{"core:ignissa", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:monere", "new-horizons:logistics", "new-horizons:logistics.scouting"},
	{"core:erdamon", "new-horizons:armorer", "new-horizons:armorer.countercharge"},
	{"core:fiur", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:kalt", "new-horizons:discipline", "new-horizons:discipline.steadfast"},
	{"core:luna", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:brissa", "new-horizons:natureMagic", "new-horizons:natureMagic.rootcaller"},
	{"core:ciele", "new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"},
	{"core:labetha", "new-horizons:natureMagic", "new-horizons:natureMagic.rootcaller"},
	{"core:inteus", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:aenain", "new-horizons:natureMagic", "new-horizons:natureMagic.rootcaller"},
	{"core:gelare", "new-horizons:estates", "new-horizons:estates.merchantPrince"},
	{"core:grindan", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:sirMullich", "new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"},
	{"core:adrienne", "new-horizons:havocMagic", "new-horizons:havocMagic.pyromancer"},
	{"core:catherine", "new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"},
	{"core:dracon", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:gelu", "new-horizons:archery", "new-horizons:archery.targetCaller"},
	{"core:kilgor", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:undeadHaart", "new-horizons:offense", "new-horizons:offense.executioner"},
	{"core:mutare", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:roland", "new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"},
	{"core:mutareDrake", "new-horizons:estates", "new-horizons:estates.prosperity"},
	{"core:boragus", "new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"core:xeron", "new-horizons:discipline", "new-horizons:discipline.espritDeCorps"},
	{"core:portraitGeneralKendal", "new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"},
	{"core:portraitYoungCristian", "new-horizons:armorer", "new-horizons:armorer.shieldMaster"},
	{"core:portraitOrdwald", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:portraitFinneas", "new-horizons:learning", "new-horizons:learning.eagleEye"},
	{"core:portraitYoungGem", "new-horizons:spellcraft", "new-horizons:spellcraft.concentration"},
	{"core:portraitYoungSandro", "new-horizons:learning", "new-horizons:learning.scholar"},
	{"core:portraitYoungYog", "new-horizons:warcasting", "new-horizons:warcasting.arcaneValor"},
}};

inline JsonNode legacyEstatesHeroes()
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	// This fixture explicitly restores the historical two-Basic-perk policy.
	// Current skills-only profiles forbid both startingPerks fields and tables.
	auto & starts = heroes["startingSkills"];
	starts["startingPerksPolicyVersion"].Integer() = 2;
	const auto perks = legacyEstatesPerks();
	for(auto & [identity, profile] : starts["startingDevelopmentProfiles"].Struct())
	{
		const auto * type = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		const auto & own = starts["factionSkills"][FactionID::encode(type->heroClass->faction.getNum())].String();
		starts["startingPerkProfiles"][identity].Vector().clear();
		profile["startingPerks"].Vector().clear();
		for(const auto & skill : profile["skills"].Vector())
		{
			const auto & key = skill["skill"].String();
			const auto & alternatives = perks["skills"][key]["perks"].Vector();
			const auto found = std::ranges::find_if(alternatives, [](const auto & row)
				{ return row["requires"].String() == "basic" && row["effect"]["status"].String() == "active"; });
			if(found == alternatives.end()) throw std::runtime_error("Missing historical Basic perk");
			JsonNode choice;
			choice["skill"].String() = key;
			choice["perk"] = (*found)["id"];
			starts["startingPerkProfiles"][identity].Vector().push_back(choice);
			if(key != own) profile["startingPerks"].Vector().push_back(choice);
		}
	}
	for(const auto & authored : HISTORICAL_GENERIC_STARTS)
	{
		JsonNode choice;
		choice["skill"].String() = authored.skill;
		choice["perk"].String() = authored.perk;
		starts["startingDevelopmentProfiles"][authored.hero]["startingPerks"].Vector() = {choice};
		for(auto & candidate : starts["startingPerkProfiles"][authored.hero].Vector())
			if(candidate["skill"].String() == authored.skill) candidate = choice;
	}
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
