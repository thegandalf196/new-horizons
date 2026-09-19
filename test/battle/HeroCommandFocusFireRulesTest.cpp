/*
 * HeroCommandFocusFireRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/HeroCommand.h"
#include <limits>

namespace
{
JsonNode legacyRules()
{
	const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
	auto rules = config["combat"]["heroCommands"];
	// A paired private profile may install v2 at this path. Deliberately author
	// the v1 fixture instead of silently testing whichever profile was mounted.
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = 1;
	rules["commands"].Struct().erase("focusFire");
	return rules;
}

JsonNode ordersOnlyRules()
{
	const JsonNode config(JsonPath::builtin("config/newHorizonsCombat"));
	return config["combat"]["heroCommands"];
}

JsonNode targetedRules()
{
	auto rules = legacyRules();
	rules["rulesetVersion"].Integer() = heroCommands::TARGETED_RULESET_VERSION;
	auto & focus = rules["commands"]["focusFire"];
	focus["kind"].String() = "order";
	focus["coverage"].String() = "ownOrdinaryShootersAtIssue";
	focus["duration"].String() = "round";
	focus["target"].String() = "enemyUnit";
	auto & formula = focus["effects"]["rangedDamagePercent"];
	formula["base"].Integer() = 20;
	formula["attack"].Float() = 0.5;
	formula["defense"].Integer() = 0;
	return rules;
}
}

TEST(HeroCommandFocusFireRules, ExistingIdsRemainStableAndNewIdIsNotADoctrine)
{
	EXPECT_EQ(static_cast<int>(HeroCommand::CHARGE), 1);
	EXPECT_EQ(static_cast<int>(HeroCommand::DEFENSIVE), 5);
	EXPECT_EQ(static_cast<int>(HeroCommand::FOCUS_FIRE), 6);
	EXPECT_EQ(heroCommands::key(HeroCommand::FOCUS_FIRE), "focusFire");
	EXPECT_FALSE(heroCommands::isDoctrine(HeroCommand::FOCUS_FIRE));
	EXPECT_TRUE(heroCommands::valid(HeroCommand::CHARGE));
	EXPECT_FALSE(heroCommands::valid(HeroCommand::ADVANCE));
	EXPECT_FALSE(heroCommands::valid(HeroCommand::AGGRESSIVE));
	EXPECT_FALSE(heroCommands::valid(static_cast<HeroCommand>(7)));
}

TEST(HeroCommandFocusFireRules, LegacyExtraKeyCannotEnableTargetedOrder)
{
	auto rules = legacyRules();
	const auto v2 = targetedRules();
	rules["commands"]["focusFire"] = v2["commands"]["focusFire"];
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE));
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::ADVANCE,
		HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
		EXPECT_EQ(heroCommands::supportedByRules(rules, command),
			command == HeroCommand::CHARGE || command == HeroCommand::HOLD_THE_LINE);
}

TEST(HeroCommandFocusFireRules, OrdersOnlyV3RejectsLegacyDoctrineCommands)
{
	const auto rules = ordersOnlyRules();
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_EQ(rules["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::FOCUS_FIRE})
		EXPECT_TRUE(heroCommands::supportedByRules(rules, command));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::AGGRESSIVE));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::DEFENSIVE));
}

TEST(HeroCommandFocusFireRules, V2UsesExactTargetedDefinitionAndExistingCoefficient)
{
	const auto rules = targetedRules();
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_TRUE(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE));
	const auto & formula = rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	EXPECT_EQ(heroCommands::coefficient(formula, 20, 999), 30);
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::NONE));
}

TEST(HeroCommandFocusFireRules, V2RejectsFloatVersionRatherThanCoercingIt)
{
	auto rules = targetedRules();
	rules["rulesetVersion"].Float() = 2.0;
	const JsonNode before = rules;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE));
	EXPECT_EQ(rules, before);
	rules = targetedRules();
	rules["schemaVersion"].Float() = 1.0;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}

TEST(HeroCommandFocusFireRules, LegacyVersionRepresentationAndAbsenceRemainUnchanged)
{
	auto rules = legacyRules();
	// Legacy Integer() accepted fractional values truncating to one. Keep that
	// compatibility local to v1; do not extend it to the new strict schema.
	rules["schemaVersion"].Float() = 1.5;
	rules["rulesetVersion"].Float() = 1.5;
	const JsonNode before = rules;
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_EQ(rules, before);
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE));
	const JsonNode null;
	JsonNode empty;
	empty.Struct();
	EXPECT_NO_THROW(heroCommands::validateRules(null));
	EXPECT_NO_THROW(heroCommands::validateRules(empty));
	EXPECT_FALSE(heroCommands::supportedByRules(null, HeroCommand::FOCUS_FIRE));
	EXPECT_FALSE(heroCommands::supportedByRules(empty, HeroCommand::CHARGE));
}

TEST(HeroCommandFocusFireRules, V2RejectsMissingAndExtraCommandsOrDefinitionFields)
{
	auto rules = targetedRules();
	rules["commands"].Struct().erase("focusFire");
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	rules = targetedRules();
	rules["commands"]["other"].Struct();
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	rules = targetedRules();
	rules["commands"]["charge"]["target"].String() = "enemyUnit";
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	rules = targetedRules();
	rules["commands"]["focusFire"]["extra"].Bool() = true;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}

TEST(HeroCommandFocusFireRules, V2RejectsWrongTargetCoverageDurationAndKind)
{
	for(const auto * field : {"target", "coverage", "duration", "kind"})
	{
		auto rules = targetedRules();
		rules["commands"]["focusFire"][field].String() = "invalid";
		EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error) << field;
	}
}

TEST(HeroCommandFocusFireRules, V2RejectsNonRangedEffectsAndMalformedCoefficients)
{
	auto rules = targetedRules();
	rules["commands"]["focusFire"]["effects"]["speedPercent"].Struct();
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	rules = targetedRules();
	rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"].Struct().erase("attack");
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	for(const double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
		heroCommands::MAX_TARGETED_COEFFICIENT + 1, -heroCommands::MAX_TARGETED_COEFFICIENT - 1})
	{
		rules = targetedRules();
		rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"]["base"].Float() = bad;
		EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
	}
}

TEST(HeroCommandFocusFireRules, EveryCoefficientRejectsNonNumericAndUnsafeValues)
{
	std::vector<JsonNode> badKinds(5);
	badKinds[1].Bool() = true;
	badKinds[2].String() = "20";
	badKinds[3].Struct();
	badKinds[4].Vector();
	for(const auto * term : {"base", "attack", "defense"})
	{
		for(const auto & bad : badKinds)
		{
			auto rules = targetedRules();
			rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"][term] = bad;
			EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error) << term;
		}
		for(const double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
			heroCommands::MAX_TARGETED_COEFFICIENT + 1, -heroCommands::MAX_TARGETED_COEFFICIENT - 1})
		{
			auto rules = targetedRules();
			rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"][term].Float() = bad;
			EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error) << term;
		}
		for(const auto boundary : {-heroCommands::MAX_TARGETED_COEFFICIENT, heroCommands::MAX_TARGETED_COEFFICIENT})
		{
			auto rules = targetedRules();
			rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"][term].Float() = boundary;
			EXPECT_NO_THROW(heroCommands::validateRules(rules)) << term;
		}
	}
}

TEST(HeroCommandFocusFireRules, LegacyWideCoefficientsCancelOverflowWithoutLosingResidual)
{
	auto rules = legacyRules();
	auto & formula = rules["commands"]["charge"]["effects"]["meleeDamagePercent"];
	formula["attack"].Float() = 1e308;
	formula["defense"].Float() = -1e308;
	for(const auto & [base, expected] : std::vector<std::pair<double, int>>{
		{1.5, 2}, {-1.5, -2}, {199.5, 200}, {-89.5, -90},
		{std::nextafter(1.5, 0.0), 1}, {std::nextafter(1.5, 2.0), 2},
		{std::nextafter(-1.5, 0.0), -1}, {std::nextafter(-1.5, -2.0), -2},
		{std::numeric_limits<double>::denorm_min(), 0}, {0, 0}})
	{
		formula["base"].Float() = base;
		ASSERT_NO_THROW(heroCommands::validateRules(rules)); // Do not narrow v1 admission.
		EXPECT_EQ(heroCommands::coefficient(formula, 2, 2), expected);
	}
	formula["base"].Float() = 1.5;
	EXPECT_EQ(heroCommands::coefficient(formula, 1, 1), 0); // Preserve the old finite result, not exact2.
	formula["base"].Float() = 0;
	EXPECT_EQ(heroCommands::coefficient(formula, 3, 2), heroCommands::MAX_EFFECT_PERCENT);
	EXPECT_EQ(heroCommands::coefficient(formula, 2, 3), heroCommands::MIN_EFFECT_PERCENT);
	EXPECT_EQ(heroCommands::coefficient(formula, std::numeric_limits<int>::min(),
		std::numeric_limits<int>::min()), 0);
	formula["defense"].Float() = 1e308;
	EXPECT_EQ(heroCommands::coefficient(formula, std::numeric_limits<int>::max(),
		std::numeric_limits<int>::min()), heroCommands::MIN_EFFECT_PERCENT);
}

TEST(HeroCommandFocusFireRules, DifferentSignificandsAndExponentsCancelExactlyInOverflowFallback)
{
	JsonNode formula;
	formula["base"].Float() = 1.5;
	formula["attack"].Float() = std::ldexp(1.5, 1023);
	formula["defense"].Float() = -std::ldexp(1.0, 1023);
	// 2 * (1.5 * 2^1023) == 3 * 2^1023, both products overflow binary64.
	EXPECT_EQ(heroCommands::coefficient(formula, 2, 3), 2);
	formula["base"].Float() = -1.5;
	EXPECT_EQ(heroCommands::coefficient(formula, 2, 3), -2);
}

TEST(HeroCommandFocusFireRules, OrdinaryFiniteCoefficientRoundingRemainsUnchanged)
{
	JsonNode formula;
	formula["base"].Float() = 20.5;
	formula["attack"].Float() = 0.5;
	formula["defense"].Float() = -0.25;
	for(const int attack : {0, 1, 20, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
		for(const int defense : {0, 1, 20, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
		{
			const double oldValue = 20.5 + 0.5 * attack - 0.25 * defense;
			const int oldResult = static_cast<int>(std::lround(std::clamp(oldValue, -90.0, 200.0)));
			EXPECT_EQ(heroCommands::coefficient(formula, attack, defense), oldResult);
		}
}

TEST(HeroCommandFocusFireRules, InvalidNonfiniteCoefficientCannotReachIntegerConversion)
{
	JsonNode formula;
	formula["base"].Float() = 0;
	formula["attack"].Float() = 0;
	formula["defense"].Float() = 0;
	for(const char * term : {"base", "attack", "defense"})
	{
		for(const double invalid : {std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
		{
			formula[term].Float() = invalid;
			EXPECT_THROW(heroCommands::coefficient(formula, 2, 2), std::runtime_error);
			EXPECT_THROW(heroCommands::coefficient(formula, 0, 0), std::runtime_error);
		}
		formula[term].Float() = 0;
	}
}

TEST(HeroCommandFocusFireRules, UntargetedV2CommandRejectsUnsupportedEffect)
{
	auto rules = targetedRules();
	const auto formula = rules["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	rules["commands"]["charge"]["effects"]["unsupportedEffect"] = formula;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}
