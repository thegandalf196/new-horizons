/*
 * NewHorizonsSorceryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"

#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/json/JsonUtils.h"
#include "HistoricalCrusadeRules.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <limits>

TEST(NewHorizonsSorceryTest, CurrentFocusRejectsRetiredMarkProfiles)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_TRUE(newHorizonsSorceryRevision::usesFocusEnchantment(rules));
	auto & profile = rules["spells"]["new-horizons:focusMagic"]["focusMagic"];
	profile["version"].Integer() = 1;
	profile["baseBasisPoints"].Integer() = 500;
	profile["powerBasisPoints"].Integer() = 3;
	profile["capBasisPoints"].Integer() = 1200;
	EXPECT_THROW(newHorizonsSorceryRevision::usesFocusEnchantment(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsSorceryTest, PhantomIntegrityUsesTheCanonicalCapAndPerkMultiplier)
{
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(0), 2000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(100), 3500);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(200), 4000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(std::numeric_limits<int32_t>::max()), 4000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(100, true), 4375);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000, 100), 350);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1234, 100), 431);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000, 100, true), 437);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1234, 100, true), 539);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1292, 0, true), 323);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000000, 1, true), 251875);
	EXPECT_THROW(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(-1), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::phantomArmyIntegrity(-1, 0), std::invalid_argument);
}

TEST(NewHorizonsSorceryTest, PhantomDamageSplitIsNotACloneApproximation)
{
	EXPECT_EQ(newHorizonsSorcery::phantomArmyDamageTakenPercent(false), 25);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyDamageTakenPercent(true), 200);
}

TEST(NewHorizonsSorceryRevisionTest, CapturedProfilesPassRealSchemaAndRoundTripWithoutInstalledFallback)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsMagicV3", "Sorcery revision schema"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsSorceryRevision::usesFocusEnchantment(rules));
	EXPECT_EQ(rules["spells"]["new-horizons:focusMagic"]["focusMagic"]["version"].Integer(), 2);
	EXPECT_TRUE(newHorizonsSorceryRevision::slowAffectsSpeed(rules));
	EXPECT_THROW(newHorizonsSorceryRevision::validateSerialization(rules, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsSorceryRevision::validateSerialization(rules, true));
	CMemorySerializer memory;
	memory.oser & rules;
	JsonNode restored;
	memory.iser & restored;
	EXPECT_EQ(restored, rules);
	EXPECT_TRUE(newHorizonsSorceryRevision::usesFocusEnchantment(restored));
	// A pre-revision v3 capture stays absent through the same JSON serialization.
	rules["spells"]["new-horizons:focusMagic"].Struct().erase("focusMagic");
	rules["spells"]["core:slow"].Struct().erase("slow");
	rules["spells"].Struct().erase("new-horizons:summonLivingArmor");
	CMemorySerializer historical;
	historical.oser & rules;
	historical.iser & restored;
	EXPECT_FALSE(newHorizonsSorceryRevision::usesFocusEnchantment(restored));
	EXPECT_FALSE(newHorizonsSorceryRevision::slowAffectsSpeed(restored));
	EXPECT_NO_THROW(newHorizonsSorceryRevision::validateSerialization(restored, false));
}

TEST(NewHorizonsSorceryRevisionTest, RejectsMalformedMisplacedAndUnsupportedRevisionClauses)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto * clause : {"focusMagic", "slow", "livingArmor"})
	{
		auto malformed = rules;
		malformed["spells"]["core:haste"][clause] = JsonNode(JsonMap{});
		EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	}
	for(const auto * identity : {"new-horizons:focusMagic", "core:slow", "new-horizons:summonLivingArmor"})
	{
		const char * clause = std::string(identity) == "core:slow" ? "slow"
			: std::string(identity) == "new-horizons:focusMagic" ? "focusMagic" : "livingArmor";
		for(const auto invalid : {JsonNode(), JsonNode(false), JsonNode(1)})
		{
			auto malformed = rules;
			malformed["spells"][identity][clause] = invalid;
			EXPECT_FALSE(JsonUtils::validate(malformed, "vcmi:newHorizonsMagicV3", "invalid Sorcery profile"));
			EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
		}
		auto unsupported = rules;
		unsupported["spells"][identity][clause]["version"].Integer() = 3;
		EXPECT_THROW(newHorizonsMagic::validateRules(unsupported), std::runtime_error);
	}
	auto inverted = rules;
	inverted["spells"]["new-horizons:focusMagic"]["focusMagic"]["baseBasisPoints"].Integer() = 1201;
	EXPECT_THROW(newHorizonsMagic::validateRules(inverted), std::runtime_error);
}

TEST(NewHorizonsSorceryRevisionTest, HistoricalCrusadeFixtureRestoresPreSorceryRosterWithoutMutatingInput)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode current(JsonPath::builtin("config/newHorizonsMagic"));
	for(const int version : {2, 3})
	{
		const auto historical = historicalCrusadeRules(current, version);
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(historical));
		EXPECT_FALSE(historical["spells"].Struct().contains("new-horizons:theChosen"));
		EXPECT_FALSE(historical["spells"].Struct().contains("new-horizons:summonLivingArmor"));
		EXPECT_FALSE(newHorizonsSorceryRevision::usesFocusEnchantment(historical));
		EXPECT_FALSE(newHorizonsSorceryRevision::slowAffectsSpeed(historical));
		EXPECT_FALSE(historical["spells"]["core:haste"].Struct().contains("ordinaryAcquisition"));
		EXPECT_EQ(historical["spells"]["new-horizons:focusMagic"]["level"].Integer(), 3);
		EXPECT_EQ(historical["spells"]["new-horizons:focusMagic"]["costs"].Vector()[3].Integer(), 11);
		EXPECT_EQ(historical["spells"]["core:slow"]["level"].Integer(), 1);
		EXPECT_EQ(historical["spells"]["core:slow"]["costs"].Vector()[3].Integer(), 3);
		EXPECT_EQ(historical["spells"]["new-horizons:massSlow"]["level"].Integer(), 1);
		EXPECT_EQ(historical["spells"]["new-horizons:massSlow"]["costs"].Vector()[3].Integer(), 9);
	}
	EXPECT_TRUE(newHorizonsSorceryRevision::usesFocusEnchantment(current));
	EXPECT_TRUE(newHorizonsSorceryRevision::slowAffectsSpeed(current));
	EXPECT_TRUE(current["spells"].Struct().contains("new-horizons:summonLivingArmor"));
}
