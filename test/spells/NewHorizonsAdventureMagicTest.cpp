/*
 * NewHorizonsAdventureMagicTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */

#include "StdInc.h"

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
	const std::array<std::pair<const char *, int>, 5> expected = {{
		{"core:summonBoat", 20},
		{"core:waterWalk", 30},
		{"core:townPortal", 50},
		{"core:fly", 60},
		{"core:dimensionDoor", 80},
	}};

	for(const auto & [identity, cost] : expected)
	{
		const SpellID spell(SpellID::decode(identity));
		ASSERT_TRUE(spell.hasValue());
		EXPECT_TRUE(newHorizonsMagic::isAdventureSpell(savedRules, spell));
		EXPECT_TRUE(newHorizonsMagic::spellSchools(savedRules, spell).empty());
		EXPECT_EQ(newHorizonsMagic::spellLevel(savedRules, spell), 0);
		EXPECT_EQ(newHorizonsMagic::adventureSpellCost(savedRules, spell), cost);
		EXPECT_EQ(newHorizonsMagic::spellCost(savedRules, spell, 0), cost);
		EXPECT_EQ(newHorizonsMagic::spellCost(savedRules, spell, 3), cost);
		EXPECT_FALSE(savedRules["spells"].Struct().contains(identity));
	}
	EXPECT_TRUE(newHorizonsMagic::adventureSpellRulesActive(savedRules));
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
