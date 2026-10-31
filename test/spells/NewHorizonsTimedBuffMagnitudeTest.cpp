/*
 * NewHorizonsTimedBuffMagnitudeTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsTimedBuffMagnitude.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../lib/spells/CSpell.h"

TEST(NewHorizonsTimedBuffMagnitudeTest, SlowSharesBaseCapAndLateTowardZeroVariantScaling)
{
	using newHorizonsSorceryRevision::slowBaseReduction;
	using newHorizonsSorceryRevision::scaleSlowVariantValue;
	EXPECT_EQ(slowBaseReduction(0, 12650, 10, 25), 20);
	EXPECT_EQ(slowBaseReduction(100, 10000, 0, 0), 40);
	EXPECT_EQ(slowBaseReduction(1000, 12650, 10, 25), 50);
	EXPECT_DOUBLE_EQ(scaleSlowVariantValue(-31, 60), -18);
	EXPECT_DOUBLE_EQ(scaleSlowVariantValue(-31.9, 60), -19);
	EXPECT_DOUBLE_EQ(scaleSlowVariantValue(-50, 60), -30);
	EXPECT_DOUBLE_EQ(scaleSlowVariantValue(-31.9, 100), -31.9);
	EXPECT_DOUBLE_EQ(scaleSlowVariantValue(7, 60), 7);
}

TEST(NewHorizonsTimedBuffMagnitudeTest, LoadedHasteAndFortuneMasteryValuesAreReadFromTheEffectiveDefinition)
{
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto * fortune = SpellID(SpellID::FORTUNE).toSpell();
	ASSERT_NE(haste, nullptr);
	ASSERT_NE(fortune, nullptr);
	for(int level = 0; level <= 3; ++level)
	{
		const auto speed = newHorizonsMagic::configuredAdditiveTimedValue(*haste, level, BonusType::STACKS_SPEED);
		const auto luck = newHorizonsMagic::configuredAdditiveTimedValue(*fortune, level, BonusType::LUCK);
		ASSERT_TRUE(speed);
		ASSERT_TRUE(luck);
		EXPECT_DOUBLE_EQ(*speed, level < 2 ? 3 : 5);
		EXPECT_DOUBLE_EQ(*luck, level < 2 ? 1 : 2);
	}
}
