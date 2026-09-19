/*
 * NewHorizonsMagicArrowTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/constants/StringConstants.h"

namespace
{
JsonNode activeRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules["spells"]["core:magicArrow"]["directDamage"]["base"].Integer() = 20;
	rules["spells"]["core:magicArrow"]["directDamage"]["powerCoefficient"].Integer() = 20;
	return rules;
}
}

TEST(NewHorizonsMagicArrowTest, DetailedSorceryFormulaAndCapAreDeterministic)
{
	const auto rules = activeRules();
	const SpellID arrow(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(newHorizonsMagic::magicArrowOverchargeEnabled(rules, arrow));

	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, arrow, 0), 2);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, arrow, 100), 4);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, arrow, 150), 5);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, arrow, 1000), 5);

	// With the New Horizons primary-rating divisor of ten, SP 24 still means
	// the declared 20 + 2*24 = 68 raw damage.
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 24, 10, 0), 68);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 10, 0), 220);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 10, 1), 253);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 10, 2), 286);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 10, 3), 319);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 10, 4), 352);
}

TEST(NewHorizonsMagicArrowTest, LegacyAndWrongSpellDoNotGainOvercharge)
{
	const JsonNode legacy;
	const SpellID arrow(SpellID::MAGIC_ARROW);
	const SpellID bolt(SpellID::LIGHTNING_BOLT);

	EXPECT_FALSE(newHorizonsMagic::magicArrowOverchargeEnabled(legacy, arrow));
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(legacy, arrow, 100), 0);
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(legacy, arrow, 100, 1, 0).has_value());

	JsonNode v1(JsonPath::builtin("config/newHorizonsMagic"));
	v1["rulesetVersion"].Integer() = 1;
	v1["spells"]["core:magicArrow"].Struct().erase("directDamage");
	EXPECT_FALSE(newHorizonsMagic::magicArrowOverchargeEnabled(v1, arrow));
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(v1, arrow, 100, 1, 0).has_value());

	const auto rules = activeRules();
	EXPECT_FALSE(newHorizonsMagic::magicArrowOverchargeEnabled(rules, bolt));
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, bolt, 100), 0);
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, bolt, 100, 1, 1).has_value());
}

TEST(NewHorizonsMagicArrowTest, V2WithoutSavedFormulaDoesNotActivate)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules["spells"]["core:magicArrow"].Struct().erase("directDamage");
	const SpellID arrow(SpellID::MAGIC_ARROW);

	EXPECT_FALSE(newHorizonsMagic::magicArrowOverchargeEnabled(rules, arrow));
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 1, 0).has_value());
}

TEST(NewHorizonsMagicArrowTest, IllegalSelectionsAreRejectedWithoutProducingDamage)
{
	const auto rules = activeRules();
	const SpellID arrow(SpellID::MAGIC_ARROW);

	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 1, -1).has_value());
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 1, 5).has_value());
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, arrow, 100, 0, 0).has_value());
}
