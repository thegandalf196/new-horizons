/*
 * NewHorizonsMagicV2RulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
constexpr auto arrowKey = "core:magicArrow";
JsonNode originalRules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
}

JsonNode legacyRules()
{
	auto rules = originalRules();
	rules["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	rules.Struct().erase("spellPoints");
	rules.Struct().erase("warcasting");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	return rules;
}

JsonNode formulaRules()
{
	auto rules = originalRules();
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	// Existing registered identity for rules-only tests; this does not alter the
	// installed spell or activate the proposed new Magic Missile definition.
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = 20;
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = 20;
	return rules;
}
}

TEST(NewHorizonsMagicV2RulesTest, ActualV1AndV2DecodeWithoutChangingExistingSchoolsLevelsOrCosts)
{
	const auto old = legacyRules();
	const auto current = formulaRules();
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(old));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(current));
	const SpellID arrow(SpellID::decode(arrowKey));
	ASSERT_NE(arrow, SpellID::NONE);
	EXPECT_EQ(newHorizonsMagic::activeSchools(current), newHorizonsMagic::activeSchools(old));
	EXPECT_EQ(newHorizonsMagic::spellSchools(current, arrow), newHorizonsMagic::spellSchools(old, arrow));
	EXPECT_EQ(newHorizonsMagic::spellLevel(current, arrow), newHorizonsMagic::spellLevel(old, arrow));
	for(int rank = 0; rank <= 3; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(current, arrow, rank), newHorizonsMagic::spellCost(old, arrow, rank));
}

TEST(NewHorizonsMagicV2RulesTest, V1StillRejectsFormulaAndUnsupportedEnvelopeFails)
{
	auto rules = formulaRules();
	rules["rulesetVersion"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, arrowKey), std::runtime_error);
	rules = formulaRules();
	rules["rulesetVersion"].Integer() = 3;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["rulesetVersion"].Float() = 2.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, arrowKey), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, V2RejectsMalformedFormulaBeforeUse)
{
	auto rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["base"].Float() = 20.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = -1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["other"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, ActiveSpellMarkerMustBeBooleanAndOldRowsStayValid)
{
	auto rules = formulaRules();
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["spells"]["core:clone"].Struct().erase("active");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["spells"]["core:clone"]["active"].String() = "false";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, WarcastingOptInIsOptionalAndStrictlyBoolean)
{
	auto rules = formulaRules();
	rules.Struct().erase("warcasting");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].Bool() = false;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].Bool() = true;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].String() = "true";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["warcasting"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["warcasting"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, SpellPointOptInIsV2OnlyAndUsesSavedCapacityPercent)
{
	auto rules = originalRules();
	ASSERT_TRUE(rules["spellPoints"].isStruct());
	EXPECT_TRUE(newHorizonsMagic::spellPointRulesActive(rules));
	EXPECT_EQ(newHorizonsMagic::spellPointsIntelligenceMaximumPercent(rules), 130);

	rules["spellPoints"]["intelligenceMaximumPercent"].Integer() = 175;
	EXPECT_EQ(newHorizonsMagic::spellPointsIntelligenceMaximumPercent(rules), 175);

	auto absent = rules;
	absent.Struct().erase("spellPoints");
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(absent));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(absent));

	auto legacy = rules;
	legacy["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(legacy));
	EXPECT_THROW(newHorizonsMagic::validateRules(legacy), std::runtime_error)
		<< "The opt-in must not silently change an existing v1 snapshot";

	auto malformed = rules;
	malformed["spellPoints"]["intelligenceMaximumPercent"].Float() = 175.0;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(malformed));
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = rules;
	malformed["spellPoints"]["intelligenceMaximumPercent"].Integer() = 99;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(malformed));
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, AbsentSnapshotRowAndOptionalFormulaNeverUseInstalledDamage)
{
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(JsonNode(), arrowKey));
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(JsonNode(JsonMap{}), arrowKey));
	EXPECT_TRUE(newHorizonsMagic::spellDirectDamage(originalRules(), arrowKey));
	auto rules = formulaRules();
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(rules, "new-horizons:magicMissile"));
	constexpr auto optionalFormulaKey = "core:armageddon";
	ASSERT_TRUE(newHorizonsMagic::spellDirectDamage(rules, optionalFormulaKey));
	rules["spells"][optionalFormulaKey].Struct().erase("directDamage");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(rules, optionalFormulaKey));
	EXPECT_FALSE(newHorizonsMagic::directDamageValue(rules, optionalFormulaKey, 24, 10));
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, "magicArrow"), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, FullValidationRetainsEntityResolutionAndRequiredCommonCoverage)
{
	auto rules = formulaRules();
	rules["spells"]["core:nonexistentFormulaSpell"] = rules["spells"][arrowKey];
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"].Struct().erase(arrowKey);
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, SavedRowAccessUsesWidePrimitiveAndReturnsIndependentValue)
{
	auto rules = formulaRules();
	const auto captured = newHorizonsMagic::spellDirectDamage(rules, arrowKey);
	ASSERT_TRUE(captured);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 10), 30);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 24, 10), 68);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 96, 10), 212);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 1), 120);
	EXPECT_THROW(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 0), std::runtime_error);
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = 100;
	EXPECT_EQ(captured->evaluate(5, 10), 30);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 10), 110);
}

TEST(NewHorizonsMagicV2RulesTest, JsonSnapshotRoundTripRetainsFormulaNotLaterSourceValues)
{
	auto source = formulaRules();
	CMemorySerializer wire;
	wire.oser & source;
	source["spells"][arrowKey]["directDamage"]["base"].Integer() = 999;
	JsonNode restored;
	wire.iser & restored;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(restored));
	EXPECT_EQ(newHorizonsMagic::directDamageValue(restored, arrowKey, 24, 10), 68);
	// JsonNode round-trip only, not actual CGameState/BattleStart entrypoints.
}
