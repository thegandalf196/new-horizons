/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/json/JsonNode.h"
#include <map>
#include <set>

namespace
{
using namespace newHorizonsHeroes;

JsonNode development() { return JsonNode(JsonPath::builtin("config/newHorizonsHeroes")); }

StartingHeroContext context(JsonNode rules = development())
{
	StartingHeroContext result;
	result.development = std::move(rules);
	result.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	PerkState perks;
	perks.replaceRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	result.perks = perks.getRules();
	result.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return result;
}

const CHero & hero(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}

std::set<std::string> perks(const StartingHeroProjection & view)
{
	std::set<std::string> result;
	for(const auto & perk : view.perks)
		result.insert(perk.perkId);
	return result;
}
}

using RampartStartingProfiles = vcmiTestData::OriginalDataTest;

TEST_F(RampartStartingProfiles, AerisStartsWithFleetfootAndClancyWithHuntingPack)
{
	if(!HeroTypeID::decode("core:aeris"))
		GTEST_SKIP() << "Requires New Horizons content";
	// Perk identities keep their historical keys: Fleetfoot is forestSFavor, Hunting Pack elvenPrecision.
	EXPECT_EQ(perks(projectStartingHero(hero("core:aeris"), context())),
		std::set<std::string>{"new-horizons:sylvanLuck.forestSFavor"});
	EXPECT_EQ(perks(projectStartingHero(hero("core:clancy"), context())),
		std::set<std::string>{"new-horizons:sylvanLuck.elvenPrecision"});
	EXPECT_EQ(perks(projectStartingHero(hero("core:kyrre"), context())),
		std::set<std::string>{"new-horizons:logistics.pathfinding"}) << "Kyrre, listed first, keeps her start";
}

TEST_F(RampartStartingProfiles, HistoricalCaptureKeepsPathfindingStarts)
{
	if(!HeroTypeID::decode("core:aeris"))
		GTEST_SKIP() << "Requires New Horizons content";
	auto rules = development();
	JsonNode pathfinding;
	pathfinding["skill"].String() = "new-horizons:logistics";
	pathfinding["perk"].String() = "new-horizons:logistics.pathfinding";
	for(const auto * key : {"core:aeris", "core:clancy"})
		rules["startingSkills"]["startingPerkProfiles"][key].Vector() = {pathfinding};
	EXPECT_NO_THROW(validateStartingPerkProfiles(rules["startingSkills"]));
	EXPECT_EQ(perks(projectStartingHero(hero("core:aeris"), context(rules))),
		std::set<std::string>{"new-horizons:logistics.pathfinding"});
}

TEST_F(RampartStartingProfiles, NoTwoRampartHeroesShareBothSkillsAndThePerk)
{
	const auto rules = development();
	const auto & starts = rules["startingSkills"];
	std::map<std::pair<std::set<std::string>, std::string>, std::string> seen;
	for(const auto & [identity, profile] : starts["startingDevelopmentProfiles"].Struct())
	{
		const auto * prototype = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		if(!prototype || prototype->special || prototype->heroClass->faction != FactionID::RAMPART)
			continue;
		std::set<std::string> skills;
		for(const auto & row : profile["skills"].Vector())
			skills.insert(row["skill"].String());
		const auto & starting = starts["startingPerkProfiles"][identity].Vector();
		const auto key = std::pair(skills, starting.empty() ? std::string() : starting.front()["perk"].String());
		const auto [existing, inserted] = seen.emplace(key, identity);
		EXPECT_TRUE(inserted) << identity << " repeats " << existing->second;
	}
	EXPECT_EQ(seen.size(), 16u);
}

TEST_F(RampartStartingProfiles, RevisedSpecialtiesPresentTheirNameLevelValuesAndArtwork)
{
	if(!HeroTypeID::decode("core:aeris"))
		GTEST_SKIP() << "Requires New Horizons content";
	const auto rules = development();
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const std::vector<std::tuple<const char *, const char *, const char *>> expected{
		{"core:mephala", "Use the Ground", "5% at hero level 1, +0.5% per further level, up to 15%"},
		{"core:kyrre", "Elusive", "Elite guards from level 10 and Champion guards from level 20"},
		{"core:jenova", "Patroness", "25% at hero level 1, +2.5% per further level, up to 100%"},
		{"core:coronius", "Giant-Slayer", "10% at hero level 1, +0.5% per further level, up to 20%"},
		{"core:uland", "Living Remedy", "2% at hero level 1, +0.25% per further level, up to 7%"},
		{"core:gem", "Sorceress of Enroth", "at most 1 (1, +1 for every 5 hero levels, up to 5)"},
		{"core:alagar", "Brittle", "5% at hero level 1, +0.5% per further level, up to 15%"},
		{"core:melodia", "Against the Odds", "+1 Luck for that combat (+1, +1 for every 10 hero levels, up to +3)"},
		{"core:aeris", "Perfect Mount", "43% at hero level 1, +3% per further level, up to 100%"},
		{"core:ryland", "Treantmoot", "10% at hero level 1, +1% per further level, up to 30%"},
		{"core:ivor", "Forest Ambush", "+2% damage (+2% per hero level, up to 100%)"}};
	for(const auto & [key, name, scaling] : expected)
	{
		const auto presentation = resolveSpecialtyPresentation(hero(key), rules, magic, std::nullopt);
		EXPECT_EQ(presentation.name, name) << key;
		EXPECT_NE(presentation.description.find(scaling), std::string::npos) << key << ": " << presentation.description;
		EXPECT_TRUE(presentation.artwork.has_value() && !presentation.artwork->empty()) << key;
	}
}
