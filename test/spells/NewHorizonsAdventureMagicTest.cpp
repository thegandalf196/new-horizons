/*
 * NewHorizonsAdventureMagicTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */

#include "StdInc.h"

#include "../../lib/ResourceSet.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
JsonNode rules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
}
}

TEST(NewHorizonsAdventureMagicTest, CanonicalRosterIsNeutralAndHasFixedCosts)
{
	const auto savedRules = rules();
	struct ExpectedAdventureSpell
	{
		const char * identity;
		int guildLevel;
		int castCost;
		int goldUnlockCost;
		int rareResourceUnlockCost;
	};
	const std::array<ExpectedAdventureSpell, 5> expected = {{
		{"core:summonBoat", 1, 20, 2500, 1},
		{"core:waterWalk", 2, 30, 5000, 2},
		{"core:townPortal", 3, 50, 10000, 4},
		{"core:fly", 4, 60, 15000, 6},
		{"core:dimensionDoor", 5, 80, 25000, 10},
	}};

	for(const auto & entry : expected)
	{
		const SpellID spell(SpellID::decode(entry.identity));
		ASSERT_TRUE(spell.hasValue());
		EXPECT_TRUE(newHorizonsMagic::isAdventureSpell(savedRules, spell));
		EXPECT_TRUE(newHorizonsMagic::spellSchools(savedRules, spell).empty());
		EXPECT_EQ(newHorizonsMagic::spellLevel(savedRules, spell), 0);
		EXPECT_EQ(newHorizonsMagic::adventureSpellCost(savedRules, spell), entry.castCost);
		EXPECT_EQ(newHorizonsMagic::spellCost(savedRules, spell, 0), entry.castCost);
		EXPECT_EQ(newHorizonsMagic::spellCost(savedRules, spell, 3), entry.castCost);
		EXPECT_FALSE(savedRules["spells"].Struct().contains(entry.identity));

		EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(savedRules, entry.guildLevel), spell);
		const auto tier = newHorizonsMagic::adventureSpellGuildLevel(savedRules, spell);
		ASSERT_TRUE(tier.has_value());
		EXPECT_EQ(*tier, entry.guildLevel);

		ResourceSet expectedUnlockCost;
		expectedUnlockCost[EGameResID::GOLD] = entry.goldUnlockCost;
		for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
			expectedUnlockCost[resource] = entry.rareResourceUnlockCost;
		EXPECT_EQ(newHorizonsMagic::adventureSpellUnlockCost(savedRules, spell), expectedUnlockCost);
	}
	EXPECT_TRUE(newHorizonsMagic::adventureSpellRulesActive(savedRules));
}

TEST(NewHorizonsAdventureMagicTest, InvalidUnavailableAndLegacyQueriesFailClosed)
{
	const auto savedRules = rules();
	EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(savedRules, 0), SpellID::NONE);
	EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(savedRules, 6), SpellID::NONE);
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(savedRules, SpellID(SpellID::MAGIC_ARROW)).has_value());
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(savedRules, SpellID::NONE).has_value());
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(savedRules, SpellID(SpellID::MAGIC_ARROW)), std::runtime_error);
	const SpellID invalidPositive(std::numeric_limits<int32_t>::max());
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(savedRules, invalidPositive).has_value());
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(savedRules, invalidPositive), std::runtime_error);

	auto invalidTierRules = rules();
	invalidTierRules["adventureSpells"]["core:townPortal"]["guildLevel"] = JsonNode(6);
	EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(invalidTierRules, 3), SpellID::NONE);
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(invalidTierRules, SpellID(SpellID::TOWN_PORTAL)).has_value());

	auto unavailableTierRules = rules();
	unavailableTierRules["adventureSpells"].Struct().erase("core:townPortal");
	EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(unavailableTierRules, 3), SpellID::NONE);
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(unavailableTierRules, SpellID(SpellID::TOWN_PORTAL)).has_value());

	const JsonNode legacy;
	EXPECT_EQ(newHorizonsMagic::adventureSpellForGuildLevel(legacy, 1), SpellID::NONE);
	EXPECT_FALSE(newHorizonsMagic::adventureSpellGuildLevel(legacy, SpellID(SpellID::SUMMON_BOAT)).has_value());
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(legacy, SpellID(SpellID::SUMMON_BOAT)), std::runtime_error);
}

TEST(NewHorizonsAdventureMagicTest, MissingOrMalformedUnlockCostIsRejected)
{
	const SpellID spell(SpellID::SUMMON_BOAT);
	auto missing = rules();
	missing["adventureSpells"]["core:summonBoat"].Struct().erase("unlockCost");
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(missing, spell), std::runtime_error);

	auto malformed = rules();
	malformed["adventureSpells"]["core:summonBoat"]["unlockCost"]["gems"] = JsonNode(-1);
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(malformed, spell), std::runtime_error);

	auto missingResource = rules();
	missingResource["adventureSpells"]["core:summonBoat"]["unlockCost"].Struct().erase("mercury");
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(missingResource, spell), std::runtime_error);

	auto fractionalResource = rules();
	fractionalResource["adventureSpells"]["core:summonBoat"]["unlockCost"]["gold"] = JsonNode(2500.5);
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(fractionalResource, spell), std::runtime_error);

	auto unknownResource = rules();
	unknownResource["adventureSpells"]["core:summonBoat"]["unlockCost"]["wood"] = JsonNode(1);
	EXPECT_THROW(newHorizonsMagic::adventureSpellUnlockCost(unknownResource, spell), std::runtime_error);
}

TEST(NewHorizonsAdventureMagicTest, UnlockCostUsesSavedSnapshotValues)
{
	const SpellID spell(SpellID::SUMMON_BOAT);
	auto savedRules = rules();
	auto & savedCost = savedRules["adventureSpells"]["core:summonBoat"]["unlockCost"];
	savedCost["gold"] = JsonNode(4321);
	savedCost["mercury"] = JsonNode(3);
	savedCost["sulfur"] = JsonNode(5);
	savedCost["crystal"] = JsonNode(7);
	savedCost["gems"] = JsonNode(9);

	ResourceSet expected;
	expected[EGameResID::GOLD] = 4321;
	expected[EGameResID::MERCURY] = 3;
	expected[EGameResID::SULFUR] = 5;
	expected[EGameResID::CRYSTAL] = 7;
	expected[EGameResID::GEMS] = 9;
	EXPECT_EQ(newHorizonsMagic::adventureSpellUnlockCost(savedRules, spell), expected);
}

TEST(NewHorizonsAdventureMagicTest, StateRoundTripsAndResetsExplicitly)
{
	newHorizonsMagic::AdventureSpellState source;
	source.castToday = true;

	CMemorySerializer wire;
	wire.oser & source;

	newHorizonsMagic::AdventureSpellState restored;
	wire.iser & restored;
	EXPECT_TRUE(restored.castToday);

	restored.castToday = false;
	EXPECT_FALSE(restored.castToday);
}

TEST(NewHorizonsAdventureMagicTest, LegacySnapshotDoesNotActivateNeutralRoster)
{
	const JsonNode legacy;
	const SpellID spell(SpellID::SUMMON_BOAT);
	EXPECT_FALSE(newHorizonsMagic::adventureSpellRulesActive(legacy));
	EXPECT_FALSE(newHorizonsMagic::isAdventureSpell(legacy, spell));
}
