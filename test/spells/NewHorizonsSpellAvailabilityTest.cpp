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

TEST(NewHorizonsSpellAvailabilityTest, OrdinaryAcquisitionDefaultsTrueForOlderRows)
{
	const JsonNode legacy;
	JsonNode oldProfile;
	oldProfile["spells"]["core:clone"] = JsonNode(JsonMap{});

	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(legacy, "core:haste", true));
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(oldProfile, "core:clone", true));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(legacy, "new-horizons:disease", true));

	JsonNode adventureProfile;
	adventureProfile["spells"] = JsonNode(JsonMap{});
	adventureProfile["adventureSpells"]["core:townPortal"] = JsonNode(JsonMap{});
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		adventureProfile, "core:townPortal", true));
}

TEST(NewHorizonsSpellAvailabilityTest, PaidAdventureUnlocksAreNotOrdinaryAcquisition)
{
	JsonNode paid;
	paid["rulesetVersion"] = JsonNode(3);
	paid["spells"] = JsonNode(JsonMap{});
	paid["adventureSpells"]["core:summonBoat"]["guildLevel"] = JsonNode(1);
	paid["adventureSpells"]["core:summonBoat"]["unlockCost"] = JsonNode(JsonMap{});
	EXPECT_TRUE(spellBelongsToRules(paid, "core:summonBoat", true));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(paid, "core:summonBoat", true));
	paid["adventureSpells"]["core:summonBoat"].Struct().erase("unlockCost");
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(paid, "core:summonBoat", true));
}

TEST(NewHorizonsSpellAvailabilityTest, OrdinaryAcquisitionPolicyIsSeparateFromSavedSpellAvailability)
{
	JsonNode current;
	current["spells"]["new-horizons:masterChainLightning"] = JsonNode(JsonMap{});
	current["spells"]["new-horizons:masterChainLightning"]["ordinaryAcquisition"].Bool() = false;
	current["spells"]["new-horizons:counterspell"] = JsonNode(JsonMap{});
	current["spells"]["new-horizons:counterspell"]["active"].Bool() = false;

	EXPECT_TRUE(spellBelongsToRules(current, "new-horizons:masterChainLightning", true));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		current, "new-horizons:masterChainLightning", true));
	EXPECT_FALSE(spellBelongsToRules(current, "new-horizons:counterspell", true));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		current, "new-horizons:counterspell", true));
}

TEST(NewHorizonsSpellAvailabilityTest, HeroAccessIsSeparateFromWorldMembershipAndOldRowsDefaultToAllowed)
{
	JsonNode older;
	older["rulesetVersion"].Integer() = 3;
	older["spells"]["core:stoneSkin"] = JsonNode(JsonMap{});

	EXPECT_TRUE(spellBelongsToRules(older, "core:stoneSkin", true));
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(older, "core:stoneSkin", true));

	JsonNode current = older;
	current["spells"]["core:stoneSkin"]["heroAccess"].Bool() = false;
	current["spells"]["core:stoneSkin"]["ordinaryAcquisition"].Bool() = false;
	EXPECT_TRUE(spellBelongsToRules(current, "core:stoneSkin", true))
		<< "heroAccess changes hero permission without removing the spell/effect from the saved world";
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(current, "core:stoneSkin", true));
}

TEST(NewHorizonsSpellAvailabilityTest, InvalidHeroAccessMarkerRejectsInsteadOfDefaultingToAllowed)
{
	for(const auto invalidMarker : {JsonNode(), JsonNode("false"), JsonNode(0)})
	{
		JsonNode invalid;
		invalid["rulesetVersion"].Integer() = 3;
		invalid["spells"]["core:stoneSkin"] = JsonNode(JsonMap{});
		invalid["spells"]["core:stoneSkin"]["heroAccess"] = invalidMarker;
		EXPECT_THROW(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
			invalid, "core:stoneSkin", true), std::runtime_error);
	}
}

TEST(NewHorizonsSpellAvailabilityTest, InvalidOrdinaryAcquisitionMarkerRejects)
{
	JsonNode invalid;
	invalid["spells"]["core:clone"] = JsonNode(JsonMap{});
	invalid["spells"]["core:clone"]["ordinaryAcquisition"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(invalid, "core:clone", true), std::runtime_error);

	invalid["spells"]["core:clone"]["ordinaryAcquisition"].String() = "false";
	EXPECT_THROW(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(invalid, "core:clone", true), std::runtime_error);
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
