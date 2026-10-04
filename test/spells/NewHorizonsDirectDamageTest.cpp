/*
 * NewHorizonsDirectDamageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "effects/EffectFixture.h"
#include "../../lib/spells/NewHorizonsDirectDamage.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/ISpellMechanics.h"
#include <limits>
#include <stdexcept>

using namespace newHorizonsMagic;

namespace
{
JsonNode record()
{
	JsonNode result;
	result["directDamage"]["base"].Integer() = 20;
	result["directDamage"]["powerCoefficient"].Integer() = 20;
	return result;
}
}

TEST(NewHorizonsDirectDamageTest, V2ComputesDeclaredRatingExamples)
{
	const auto formula = directDamageFormula(record(), 2);
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->evaluate(5, 10), 30);
	EXPECT_EQ(formula->evaluate(24, 10), 68);
	EXPECT_EQ(formula->evaluate(96, 10), 212);
}

TEST(NewHorizonsDirectDamageTest, MultiplyBeforeDivisionAndPreserveFixedTerm)
{
	const DirectDamageFormula formula{20, 20};
	EXPECT_EQ(formula.evaluate(1, 3), 26);
	EXPECT_EQ(formula.evaluate(0, 10), 20);
	EXPECT_EQ(formula.evaluate(5, 1), 120) << "Legacy units use divisor1, not a second rating conversion";
}

TEST(NewHorizonsDirectDamageTest, SchoolRankScalesOnlyTheSpellPowerCoefficient)
{
	const DirectDamageFormula formula{100, 20};
	EXPECT_EQ(formula.evaluate(0, 10, 145), 100) << "Every rank preserves the fixed base";
	EXPECT_EQ(formula.evaluate(20, 10, 100), 140) << "No School Skill keeps the base coefficient";
	EXPECT_EQ(formula.evaluate(20, 10, 115), 146) << "Basic multiplies only the Spell Power coefficient";
	EXPECT_EQ(formula.evaluate(20, 10, 130), 152) << "Advanced multiplies only the Spell Power coefficient";
	EXPECT_EQ(formula.evaluate(20, 10, 145), 158) << "Expert multiplies only the Spell Power coefficient";
	EXPECT_THROW(formula.evaluate(20, 10, 1001), std::runtime_error);
}

TEST(NewHorizonsDirectDamageTest, DamageSpecialtyPreservesBaseAndFloorsOnlyOnce)
{
	const DirectDamageFormula formula{100, 20};
	EXPECT_EQ(formula.evaluateBasisPoints(0, 3, 14500, 25, 15), 100);
	EXPECT_EQ(formula.evaluateBasisPoints(1, 3, 14500, 25, 15), 113)
		<< "Floor 20/3 * 1.45 * 1.25 * 1.15 once, then add the unchanged fixed base";
	const DirectDamageFormula fractionalBasisPoint{20, 1};
	EXPECT_EQ(fractionalBasisPoint.evaluateBasisPoints(100000, 1, 1, 0, 15), 31)
		<< "Do not truncate a basis-point coefficient multiplied by 115/100";
	EXPECT_EQ(fractionalBasisPoint.evaluateBasisPoints(100000, 1, 1, 0, 0), 30);
}

TEST(NewHorizonsDirectDamageTest, DamageSpecialtySupportsLargestDivisorAndRejectsUnknownFactor)
{
	const DirectDamageFormula largest{MAX_DIRECT_DAMAGE_PARAMETER, MAX_DIRECT_DAMAGE_PARAMETER};
	const auto maximum = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(largest.evaluateBasisPoints(maximum, maximum, 100000, 1000, 15), INT64_C(127500000));
	const int64_t denominator = static_cast<int64_t>(maximum) * INT64_C(2000000000);
	EXPECT_EQ(spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		denominator - 1, maximum, 100000, 1000, 1000, 15), INT64_C(2782999999999))
		<< "The remainder loop must not overflow signed arithmetic with a large divisor";
	EXPECT_THROW(largest.evaluateBasisPoints(1, 1, 10000, 0, 14), std::runtime_error);
	EXPECT_THROW(largest.evaluateBasisPoints(1, 1, 10000, 0, -1), std::runtime_error);
}

TEST(NewHorizonsDirectDamageTest, V1RejectsFieldButSupportedVersionsPermitAbsence)
{
	const JsonNode absent(JsonMap{});
	EXPECT_FALSE(directDamageFormula(absent, 1));
	EXPECT_FALSE(directDamageFormula(absent, 2));
	EXPECT_FALSE(directDamageFormula(absent, 3));
	EXPECT_THROW(directDamageFormula(record(), 1), std::runtime_error);
	EXPECT_TRUE(directDamageFormula(record(), 3));
	EXPECT_THROW(directDamageFormula(record(), 4), std::runtime_error);
	EXPECT_THROW(directDamageFormula(JsonNode(), 2), std::runtime_error);
}

TEST(NewHorizonsDirectDamageTest, RejectsPresentNullMissingAndUnknownFormulaFields)
{
	auto input = record();
	input["directDamage"] = JsonNode();
	EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
	input = record();
	input["directDamage"].Struct().erase("base");
	EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
	input = record();
	input["directDamage"]["divisor"].Integer() = 10;
	EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
}

TEST(NewHorizonsDirectDamageTest, RejectsFractionalNegativeOversizedAndWrongTypedParameters)
{
	for(const std::string key : {"base", "powerCoefficient"})
	{
		for(const int value : {-1, MAX_DIRECT_DAMAGE_PARAMETER + 1})
		{
			auto input = record();
			input["directDamage"][key].Integer() = value;
			EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
		}
		for(const double value : {-1.0, 0.5, 20.0, 1000001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
		{
			auto input = record();
			input["directDamage"][key].Float() = value;
			EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
		}
		auto input = record();
		input["directDamage"][key] = JsonNode();
		EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
		input["directDamage"][key].Bool() = true;
		EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
		input["directDamage"][key].String() = "20";
		EXPECT_THROW(directDamageFormula(input, 2), std::runtime_error);
	}
}

TEST(NewHorizonsDirectDamageTest, BoundedWideArithmeticAndZeroFormula)
{
	const DirectDamageFormula largest{MAX_DIRECT_DAMAGE_PARAMETER, MAX_DIRECT_DAMAGE_PARAMETER};
	EXPECT_EQ(largest.evaluate(std::numeric_limits<int32_t>::max(), 1), INT64_C(2147483648000000));
	const DirectDamageFormula zero{0, 0};
	EXPECT_EQ(zero.evaluate(std::numeric_limits<int32_t>::max(), 1), 0);
	EXPECT_EQ(largest.evaluate(std::numeric_limits<int32_t>::max(), 1, 145), INT64_C(3113851289150000));
}

TEST(NewHorizonsDirectDamageTest, RejectsInvalidEvaluationInputsInsteadOfProducingHealing)
{
	const DirectDamageFormula formula{20, 20};
	EXPECT_THROW(formula.evaluate(5, 0), std::runtime_error);
	EXPECT_THROW(formula.evaluate(5, -1), std::runtime_error);
	EXPECT_THROW(formula.evaluate(-1, 10), std::runtime_error);
	const DirectDamageFormula invalid{-1, 20};
	EXPECT_THROW(invalid.evaluate(5, 10), std::runtime_error);
}

TEST(NewHorizonsDirectDamageTest, CapturedFormulaDoesNotFollowSourceMutations)
{
	auto source = record();
	const auto captured = directDamageFormula(source, 2);
	ASSERT_TRUE(captured);
	source["directDamage"]["base"].Integer() = 100;
	EXPECT_EQ(captured->evaluate(5, 10), 30);
	EXPECT_EQ(directDamageFormula(source, 2)->evaluate(5, 10), 110);
}

TEST(NewHorizonsDirectDamageTest, DisintegrateUsesTheSavedDamageFormula)
{
	// Keep this unit test independent of the test preset's optional resource
	// mounts; the canonical JSON values are checked by the content test.
	JsonNode rules;
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules["spells"]["new-horizons:disintegrate"]["directDamage"]["base"].Integer() = 180;
	rules["spells"]["new-horizons:disintegrate"]["directDamage"]["powerCoefficient"].Integer() = 25;
	const auto formula = directDamageFormula(rules["spells"]["new-horizons:disintegrate"], 2);
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->base, 180);
	EXPECT_EQ(formula->powerCoefficient, 25);
	EXPECT_EQ(formula->evaluate(0, 10), 180);
	EXPECT_EQ(formula->evaluate(20, 10), 230);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, "new-horizons:disintegrate", 24, 10), 240);
}

TEST(NewHorizonsDirectDamageTest, LifeDrainUsesItsCanonicalFixedAndSpellPowerTerms)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	const auto formula = spellDirectDamage(rules, "new-horizons:lifeDrain");
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->base, 25);
	EXPECT_EQ(formula->powerCoefficient, 18);
	EXPECT_EQ(formula->evaluateBasisPoints(100, 10, 10000), 205)
		<< "SP 100 yields 25 + 1.8 x 100 with no rank coefficient";
	EXPECT_EQ(formula->evaluateBasisPoints(100, 10, 11500), 232);
	EXPECT_EQ(formula->evaluateBasisPoints(100, 10, 13000), 259);
	EXPECT_EQ(formula->evaluateBasisPoints(100, 10, 14500), 286);

	auto legacy = rules;
	legacy["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	legacy["spells"].Struct().erase("new-horizons:lifeDrain");
	EXPECT_FALSE(spellDirectDamage(legacy, "new-horizons:lifeDrain"))
		<< "Legacy saved rosters without the Life Drain entry do not activate its formula";
}

namespace test
{
class LifeDrainScriptTest : public ::testing::Test, public EffectFixture
{
public:
	LifeDrainScriptTest() : EffectFixture("core:lifeDrainEffect") {}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		setupEffect(JsonNode());
	}
};

TEST_F(LifeDrainScriptTest, AddsOrderedEnemyThenFriendlyCreatureTargets)
{
	std::vector<AimType> types{AimType::CREATURE};
	subject->adjustTargetTypes(types, &mechanicsMock);
	EXPECT_EQ(types, (std::vector<AimType>{AimType::CREATURE, AimType::CREATURE}));
}

TEST_F(LifeDrainScriptTest, LegacyMagicDoesNotExposeAnEffectOrPreview)
{
	EXPECT_CALL(mechanicsMock, adaptProblem(Eq(ESpellCastProblem::NO_APPROPRIATE_TARGET), Ref(problemMock)))
		.WillOnce(Return(false));
	EXPECT_FALSE(subject->applicableGeneral(problemMock, &mechanicsMock));

	const auto preview = subject->getHealthChange(&mechanicsMock, {});
	EXPECT_EQ(preview.hpDelta, 0);
	EXPECT_EQ(preview.unitsDelta, 0);

	// The script must remain a no-op for old snapshots even if invoked directly.
	subject->apply(&serverMock, &mechanicsMock, {});
}
}
