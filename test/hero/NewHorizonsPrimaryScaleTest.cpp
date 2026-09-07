/*
 * NewHorizonsPrimaryScaleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPrimaryScale.h"

TEST(NewHorizonsPrimaryScaleTest, ScalingPreservesFixedValuesAndFractionalRatingProgress)
{
	using newHorizonsHeroes::scaledPowerEffect;
	EXPECT_EQ(scaledPowerEffect(300, 75, 4, 1), 600);
	EXPECT_EQ(scaledPowerEffect(300, 75, 40, 10), 600);
	EXPECT_EQ(scaledPowerEffect(300, 75, 43, 10), 622);
	EXPECT_EQ(scaledPowerEffect(3, 0, 40, 10), 3);
	EXPECT_EQ(scaledPowerEffect(300, 75, 0, 10), 300);
}

TEST(NewHorizonsPrimaryScaleTest, KnowledgeIsBaseManaBeforeCapabilityMultiplier)
{
	using newHorizonsHeroes::manaFromKnowledge;
	EXPECT_EQ(manaFromKnowledge(20, 100), 20);
	EXPECT_EQ(manaFromKnowledge(20, 130), 26);
	EXPECT_EQ(manaFromKnowledge(0, 130), 0);
}

TEST(NewHorizonsPrimaryScaleTest, InvalidInputsRejectAndWideProductsDoNotWrap)
{
	using namespace newHorizonsHeroes;
	EXPECT_THROW(scaledPowerEffect(300, 75, 40, 0), std::runtime_error);
	EXPECT_THROW(scaledPowerEffect(300, 75, -1, 10), std::runtime_error);
	EXPECT_THROW(manaFromKnowledge(-1, 100), std::runtime_error);
	EXPECT_THROW(manaFromKnowledge(20, -1), std::runtime_error);
	const auto maximum = std::numeric_limits<int>::max();
	EXPECT_EQ(scaledPowerEffect(maximum, maximum, maximum, 1),
		static_cast<int64_t>(maximum) * maximum + maximum);
	EXPECT_EQ(manaFromKnowledge(maximum, maximum), static_cast<int64_t>(maximum) * maximum / 100);
}
