/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
#include <limits>

namespace
{
JsonNode chargeRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	auto & arrow = rules["spells"]["core:magicArrow"];
	arrow["magicArrow"]["version"].Integer() = 1;
	arrow["directDamage"]["base"].Integer() = 20;
	arrow["directDamage"]["powerCoefficient"].Integer() = 10;
	return rules;
}
const SpellID ARROW(SpellID::MAGIC_ARROW);
}

TEST(NewHorizonsMagicArrowCharge, EveryLevelUsesRawPowerAndItsActualTotalPrice)
{
	const auto rules = chargeRules();
	const newHorizonsMagic::MagicArrowOverchargeModifiers overcharger{1};
	constexpr std::array damage{220, 420, 620, 820, 1020, 1220, 1420};
	constexpr std::array cost{4, 12, 30, 50, 75, 105, 140};
	for(int q = 0; q <= 6; ++q)
	{
		SCOPED_TRACE(q);
		EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, ARROW, 200, 10, q, overcharger), damage[q]);
		EXPECT_EQ(newHorizonsMagic::magicArrowManaCost(rules, ARROW, 4, q), cost[q]);
		EXPECT_EQ(newHorizonsMagic::magicArrowManaCost(rules, ARROW, 3, q), cost[q] - 1);
		if(q <= 5)
			EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, ARROW, 200, 10, q), damage[q]);
	}
}

TEST(NewHorizonsMagicArrowCharge, SchoolSpellcraftEmpowerAndSpecialtyScaleOnlyThePowerTermOnce)
{
	const auto rules = chargeRules();
	// Expert School (145%) x Expert Spellcraft (130%) = 18850 basis points.
	EXPECT_EQ(newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW, 200, 10, 2, {}, 18850), 1151);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW, 200, 10, 2, {}, 18850, 25), 1433);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW, 200, 10, 2, {}, 18850, 0, 15), 1320);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW, 0, 10, 2, {}, 18850, 25, 15), 20);
}

TEST(NewHorizonsMagicArrowCharge, CapacityUsesEveryRawPowerBoundaryAndOnlyOneOverchargerLevel)
{
	const auto rules = chargeRules();
	for(const auto [power, expected] : std::array<std::pair<int, int>, 8>{{
		{0, 2}, {49, 2}, {50, 3}, {99, 3}, {100, 4}, {149, 4}, {150, 5}, {1000000, 5}}})
	{
		SCOPED_TRACE(power);
		EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, ARROW, power), expected);
		EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(rules, ARROW, power, {1}), expected + 1);
	}
	// An accepted contextual power bonus does not open a higher charge level.
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW, 100, 10, 3, {}, 10000, 0, 0, 49));
}

TEST(NewHorizonsMagicArrowCharge, AffordableMaximumUsesTablePricesAndExactMana)
{
	const auto rules = chargeRules();
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 4, 29), 1);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 4, 30), 2);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 3, 29), 2);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 4, 3), -1);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 4, 140), 5);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaximumAffordableOvercharge(rules, ARROW, 200, 4, 140, {1}), 6);
}

TEST(NewHorizonsMagicArrowCharge, InvalidSelectionsAndOtherSpellsNeverReceiveChargeEffects)
{
	const auto rules = chargeRules();
	for(const int q : {-1, 6, 7, std::numeric_limits<int>::max()})
		EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, ARROW, 200, 10, q));
	EXPECT_FALSE(newHorizonsMagic::magicArrowManaCost(rules, ARROW, 4, -1));
	EXPECT_FALSE(newHorizonsMagic::magicArrowManaCost(rules, ARROW, 4, 7));
	EXPECT_FALSE(newHorizonsMagic::magicArrowManaCost(rules, ARROW, std::numeric_limits<int32_t>::max(), 1));
	EXPECT_FALSE(newHorizonsMagic::magicArrowDamage(rules, SpellID::ICE_BOLT, 200, 10, 1));
	EXPECT_FALSE(newHorizonsMagic::magicArrowManaCost(rules, SpellID::ICE_BOLT, 4, 1));
}

TEST(NewHorizonsMagicArrowCharge, CurrentProfileRoundTripsAndRejectsUnsupportedSerialization)
{
	auto current = chargeRules();
	CMemorySerializer memory;
	memory.oser & current;
	JsonNode restored;
	memory.iser & restored;
	EXPECT_EQ(restored, current);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(restored, ARROW, 100, 10, 1, {1}), 220);
	EXPECT_EQ(newHorizonsMagic::magicArrowManaCost(restored, ARROW, 4, 1), 12);
	EXPECT_THROW(newHorizonsMagic::validateMagicArrowChargeSerialization(current, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsMagic::validateMagicArrowChargeSerialization(current, true));
	auto unsupported = current;
	unsupported["spells"]["core:magicArrow"].Struct().erase("magicArrow");
	EXPECT_FALSE(newHorizonsMagic::magicArrowOverchargeEnabled(unsupported, ARROW));
	EXPECT_THROW(newHorizonsMagic::validateMagicArrowSpellRow(unsupported, "core:magicArrow",
		unsupported["spells"]["core:magicArrow"]), std::runtime_error);
}

TEST(NewHorizonsMagicArrowCharge, UnknownVersionNullFractionalAndWrongIdentityProfilesReject)
{
	const auto valid = chargeRules();
	for(const int version : {0, 2})
	{
		auto invalid = valid;
		invalid["spells"]["core:magicArrow"]["magicArrow"]["version"].Integer() = version;
		EXPECT_THROW(newHorizonsMagic::validateMagicArrowChargeSerialization(invalid, true), std::runtime_error);
	}
	auto invalid = valid;
	invalid["spells"]["core:magicArrow"]["magicArrow"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateMagicArrowChargeSerialization(invalid, true), std::runtime_error);
	invalid = valid;
	invalid["spells"]["core:magicArrow"]["magicArrow"]["version"].Float() = 1.0;
	EXPECT_THROW(newHorizonsMagic::validateMagicArrowChargeSerialization(invalid, true), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::validateMagicArrowSpellRow(valid, "core:iceBolt",
		valid["spells"]["core:magicArrow"]), std::runtime_error);
}

TEST(NewHorizonsMagicArrowCharge, UnchangedDisintegrateHasTheActualCrossingAndEfficiencyRelationship)
{
	const auto rules = chargeRules();
	const auto disintegrate = newHorizonsMagic::spellDirectDamage(rules, "new-horizons:disintegrate");
	ASSERT_TRUE(disintegrate);
	ASSERT_EQ(disintegrate->base, 180);
	ASSERT_EQ(disintegrate->powerCoefficient, 25); // Rating divisor ten: 180 + 2.5P.
	const auto & costs = rules["spells"]["new-horizons:disintegrate"]["costs"].Vector();
	ASSERT_EQ(costs.size(), 4u);
	for(const auto & cost : costs)
		ASSERT_EQ(cost.Integer(), 25);
	EXPECT_EQ(disintegrate->evaluate(200, 10, 100), 680);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, ARROW, 320, 10, 2), 980);
	EXPECT_EQ(disintegrate->evaluate(320, 10, 100), 980);
	EXPECT_EQ(newHorizonsMagic::magicArrowDamage(rules, ARROW, 321, 10, 2), 983);
	EXPECT_EQ(disintegrate->evaluate(321, 10, 100), 982);
	for(const int power : {1, 35, 36, 45, 46, 49, 50, 64, 65, 99, 100, 106, 107, 149,
		150, 200, 319, 320, 321, 1000, 1000000})
		for(const int coefficient : {10000, 12650, 18850})
		{
			const auto comparison = disintegrate->evaluateBasisPoints(power, 10, coefficient);
			for(int q = 0; q <= newHorizonsMagic::magicArrowMaxOvercharge(rules, ARROW, power, {1}); ++q)
			{
				SCOPED_TRACE(std::to_string(power) + "/" + std::to_string(coefficient) + "/" + std::to_string(q));
				const auto arrow = newHorizonsMagic::magicArrowDamageBasisPoints(rules, ARROW,
					power, 10, q, {1}, coefficient);
				const auto price = newHorizonsMagic::magicArrowManaCost(rules, ARROW, 4, q);
				ASSERT_TRUE(arrow);
				ASSERT_TRUE(price);
				if(q < 2)
					EXPECT_LT(*arrow, comparison);
				else
					EXPECT_LT(*arrow * 25, comparison * *price);
			}
		}
	// For every positive P under matched SP scaling, q2..6 have no larger
	// damage-per-Mana slope and a strictly smaller constant term. The integer
	// floor cannot bridge this fixed cross-product gap (at least 4900).
	for(int q = 2; q <= 6; ++q)
	{
		const auto price = newHorizonsMagic::magicArrowManaCost(rules, ARROW, 4, q);
		ASSERT_TRUE(price);
		EXPECT_LE(25 * 10 * (1 + q), disintegrate->powerCoefficient * *price);
		EXPECT_LT(20 * 25, disintegrate->base * *price);
	}
}
