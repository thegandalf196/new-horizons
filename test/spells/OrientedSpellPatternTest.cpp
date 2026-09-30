/*
 * OrientedSpellPatternTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/OrientedSpellPattern.h"

namespace
{
constexpr std::array<int, 5> VENGEFUL_VINES_DIRECTIONS{0, 1, 0, -1, 0};
}

TEST(OrientedSpellPatternTest, AllSixRotationsContainSixDistinctConnectedHexes)
{
	const BattleHex origin(8, 5);

	for(const auto direction : BattleHex::hexagonalDirections())
	{
		const auto path = spells::makeOrientedSpellPath(origin, direction, VENGEFUL_VINES_DIRECTIONS);
		ASSERT_EQ(path.size(), 6U);
		EXPECT_EQ(path.front(), origin);

		for(size_t index = 1; index < path.size(); ++index)
			EXPECT_EQ(BattleHex::getDistance(path[index - 1], path[index]), 1);
	}
}

TEST(OrientedSpellPatternTest, RelativeOffsetsWrapAndRespectOddAndEvenRowParity)
{
	constexpr std::array<int, 1> straightStep{0};
	constexpr std::array<int, 1> positiveWrappedStep{7};
	constexpr std::array<int, 1> negativeWrappedStep{-7};
	const auto fromEvenRow = spells::makeOrientedSpellPath(BattleHex(8, 4), BattleHex::TOP_RIGHT, straightStep);
	const auto fromOddRow = spells::makeOrientedSpellPath(BattleHex(8, 5), BattleHex::TOP_RIGHT, straightStep);
	const auto positiveWrapped = spells::makeOrientedSpellPath(BattleHex(8, 5), BattleHex::RIGHT, positiveWrappedStep);
	const auto negativeWrapped = spells::makeOrientedSpellPath(BattleHex(8, 5), BattleHex::RIGHT, negativeWrappedStep);

	ASSERT_EQ(fromEvenRow.size(), 2U);
	ASSERT_EQ(fromOddRow.size(), 2U);
	ASSERT_EQ(positiveWrapped.size(), 2U);
	ASSERT_EQ(negativeWrapped.size(), 2U);
	EXPECT_EQ(fromEvenRow[1], BattleHex(9, 3));
	EXPECT_EQ(fromOddRow[1], BattleHex(8, 4));
	EXPECT_EQ(positiveWrapped[1], BattleHex(8, 6));
	EXPECT_EQ(negativeWrapped[1], BattleHex(8, 4));
}

TEST(OrientedSpellPatternTest, VengefulVinesOffsetsMakeTheExpectedSBend)
{
	const BattleHex origin(8, 5);
	const auto path = spells::makeOrientedSpellPath(origin, BattleHex::RIGHT, VENGEFUL_VINES_DIRECTIONS);
	constexpr std::array expectedDirections{
		BattleHex::RIGHT,
		BattleHex::BOTTOM_RIGHT,
		BattleHex::RIGHT,
		BattleHex::TOP_RIGHT,
		BattleHex::RIGHT};

	ASSERT_EQ(path.size(), 6U);
	for(size_t index = 0; index < expectedDirections.size(); ++index)
		EXPECT_EQ(BattleHex::mutualPosition(path[index], path[index + 1]), expectedDirections[index]);

	const auto straightPathHex = origin.cloneInDirection(BattleHex::RIGHT).cloneInDirection(BattleHex::RIGHT);
	EXPECT_NE(path[2], straightPathHex);
}

TEST(OrientedSpellPatternTest, InvalidOriginsDirectionsAndEdgesRejectTheWholePath)
{
	constexpr std::array<int, 2> twoSteps{0, 0};
	constexpr std::array<int, 2> reversingSteps{0, 3};
	const BattleHex origin(8, 5);

	EXPECT_TRUE(spells::makeOrientedSpellPath(origin, BattleHex::TOP, twoSteps).empty());
	EXPECT_TRUE(spells::makeOrientedSpellPath(BattleHex(BattleHex::INVALID), BattleHex::RIGHT, twoSteps).empty());
	EXPECT_TRUE(spells::makeOrientedSpellPath(BattleHex(0, 5), BattleHex::RIGHT, twoSteps).empty());
	EXPECT_TRUE(spells::makeOrientedSpellPath(BattleHex(2, 5), BattleHex::LEFT, twoSteps).empty());
	EXPECT_TRUE(spells::makeOrientedSpellPath(origin, BattleHex::RIGHT, reversingSteps).empty());
}

TEST(OrientedSpellPatternTest, AdjacentSpellDirectionFindsAllSixNeighbors)
{
	const BattleHex origin(8, 5);

	for(const auto direction : BattleHex::hexagonalDirections())
	{
		const BattleHex endpoint = origin.cloneInDirection(direction);
		EXPECT_EQ(spells::adjacentSpellDirection(origin, endpoint), direction);
	}

	const BattleHex nonAdjacent = origin.cloneInDirection(BattleHex::RIGHT).cloneInDirection(BattleHex::RIGHT);
	EXPECT_EQ(spells::adjacentSpellDirection(origin, nonAdjacent), BattleHex::NONE);
}
