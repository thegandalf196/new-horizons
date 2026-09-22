/*
 * NewHorizonsSpellAvailabilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

using newHorizonsMagic::spellBelongsToRules;

TEST(NewHorizonsSpellAvailabilityTest, LegacyWorldDoesNotAcquireCuratedHeroSpells)
{
	const JsonNode legacy;
	EXPECT_TRUE(spellBelongsToRules(legacy, "core:haste", true));
	EXPECT_TRUE(spellBelongsToRules(legacy, "other-mod:spell", true));
	EXPECT_FALSE(spellBelongsToRules(legacy, "new-horizons:disease", true));
	EXPECT_TRUE(spellBelongsToRules(legacy, "new-horizons:creatureAbility", false));
}

TEST(NewHorizonsSpellAvailabilityTest, LaterRosterDoesNotRetroactivelyExpandEarlierSnapshot)
{
	// Minimal membership inputs, not a claim that these are complete valid rules.
	JsonNode older;
	older["spells"]["core:haste"] = JsonNode(JsonMap{});
	JsonNode later = older;
	later["spells"]["new-horizons:disease"] = JsonNode(JsonMap{});
	EXPECT_TRUE(spellBelongsToRules(older, "core:haste", true));
	EXPECT_FALSE(spellBelongsToRules(older, "new-horizons:disease", true));
	EXPECT_TRUE(spellBelongsToRules(later, "new-horizons:disease", true));
	EXPECT_FALSE(spellBelongsToRules(older, "new-horizons:disease", true));
}

TEST(NewHorizonsSpellAvailabilityTest, ActiveMarkerExcludesOnlyTheSavedRow)
{
	const JsonNode legacy;
	JsonNode oldNewHorizons;
	oldNewHorizons["spells"]["core:clone"] = JsonNode(JsonMap{});
	JsonNode current = oldNewHorizons;
	current["spells"]["core:clone"]["active"].Bool() = false;
	current["spells"]["new-horizons:phantomArmy"] = JsonNode(JsonMap{});

	EXPECT_TRUE(spellBelongsToRules(legacy, "core:clone", true));
	EXPECT_TRUE(spellBelongsToRules(oldNewHorizons, "core:clone", true));
	EXPECT_FALSE(spellBelongsToRules(current, "core:clone", true));
	EXPECT_TRUE(spellBelongsToRules(current, "new-horizons:phantomArmy", true));
	EXPECT_FALSE(spellBelongsToRules(oldNewHorizons, "new-horizons:phantomArmy", true));
}

TEST(NewHorizonsSpellAvailabilityTest, InvalidActiveMarkerRejectsInsteadOfSilentlyActivating)
{
	JsonNode invalid;
	invalid["spells"]["core:clone"]["active"].String() = "false";
	EXPECT_THROW(spellBelongsToRules(invalid, "core:clone", true), std::runtime_error);
}

TEST(NewHorizonsSpellAvailabilityTest, MalformedCommonSpellContextRejects)
{
	EXPECT_THROW(spellBelongsToRules(JsonNode(), "unscoped", true), std::runtime_error);
	JsonNode invalid;
	invalid["schemaVersion"].Integer() = 1;
	EXPECT_THROW(spellBelongsToRules(invalid, "core:haste", true), std::runtime_error);
}
