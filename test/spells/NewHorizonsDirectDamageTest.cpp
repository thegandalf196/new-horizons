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
#include "../../lib/spells/NewHorizonsDirectDamage.h"
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

TEST(NewHorizonsDirectDamageTest, V1RejectsFieldButBothVersionsPermitAbsence)
{
	const JsonNode absent(JsonMap{});
	EXPECT_FALSE(directDamageFormula(absent, 1));
	EXPECT_FALSE(directDamageFormula(absent, 2));
	EXPECT_THROW(directDamageFormula(record(), 1), std::runtime_error);
	EXPECT_THROW(directDamageFormula(record(), 3), std::runtime_error);
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
