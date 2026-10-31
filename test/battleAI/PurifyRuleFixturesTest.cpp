/*
 * PurifyRuleFixturesTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PurifyRuleFixtures.h"
#include "../spells/HistoricalCrusadeRules.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include <stdexcept>

namespace
{
class PurifyRuleFixturesTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
};

TEST_F(PurifyRuleFixturesTest, SavedV2RecipeValidatesAndPreservesCanonicalPurifyRefusal)
{
	const auto historical = purifyTest::savedV2MagicRules();
	EXPECT_EQ(historical["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	for(const auto * key : {"heroLuck", "heroMorale", "morale", "creatureAbilities",
		"protectedAdventureBarriers", "schoolRankPowerCoefficientPercent", "spellcraftEfficiencyPercent"})
		EXPECT_FALSE(historical.Struct().contains(key));
	for(const auto * identity : {"core:waterWalk", "core:fly"})
		EXPECT_FALSE(historical["adventureSpells"][identity].Struct().contains("requireLegalDayEnd"));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(historical));
	EXPECT_FALSE(newHorizonsPurify::enabled(historical, newHorizonsPurify::spellID()));
	const JsonNode current(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_TRUE(current.Struct().contains("heroLuck"));
	EXPECT_TRUE(current.Struct().contains("heroMorale"));
	EXPECT_EQ(historical, historicalCrusadeRules(current, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
	// Existing consumers require these two spell rows to retain their own policies.
	for(const auto * identity : {"new-horizons:spellLock", "new-horizons:regeneration"})
		EXPECT_EQ(historical["spells"][identity], current["spells"][identity]);
}

TEST_F(PurifyRuleFixturesTest, HistoricalRecipeDoesNotRelaxTheProductionV3FieldGuard)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto * key : {"heroLuck", "heroMorale"})
	{
		auto invalid = purifyTest::savedV2MagicRules();
		invalid[key] = current[key];
		EXPECT_THROW(newHorizonsMagic::validateRules(invalid), std::runtime_error);
	}
	// Returning an old recipe must not admit modern revision rows under its v2 label.
	for(const auto * identity : {"new-horizons:focusMagic", "core:slow", "new-horizons:summonLivingArmor"})
	{
		auto invalid = purifyTest::savedV2MagicRules();
		invalid["spells"][identity] = current["spells"][identity];
		EXPECT_THROW(newHorizonsMagic::validateRules(invalid), std::runtime_error);
	}
}

TEST_F(PurifyRuleFixturesTest, NeutralCommandsRetainTheCompleteCurrentProfileExceptExistingNumericNeutralizer)
{
	const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
	const auto original = file["combat"]["heroCommands"];
	auto expected = original;
	for(auto & [name, command] : expected["commands"].Struct())
	{
		(void)name;
		for(auto & [kind, effect] : command["effects"].Struct())
		{
			(void)kind;
			effect["base"].Float() = 0;
			effect["attack"].Float() = 0;
			effect["defense"].Float() = 0;
		}
	}
	const auto neutral = purifyTest::neutralSpellCompetitionCommands();
	EXPECT_EQ(neutral, expected);
	EXPECT_NO_THROW(heroCommands::validateRules(neutral));
	EXPECT_NE(neutral, JsonNode());
	EXPECT_EQ(file["combat"]["heroCommands"], original);
}
}
