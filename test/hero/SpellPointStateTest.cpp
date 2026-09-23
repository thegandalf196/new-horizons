/*
 * SpellPointStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/SpellPointState.h"

#include <array>

using newHorizonsHeroes::SpellPointState;

namespace
{
struct PoolArchive
{
	bool saving;
	std::array<int32_t, 2> values{};
	size_t index = 0;

	void operator&(int32_t & value)
	{
		if(saving)
			values.at(index++) = value;
		else
			value = values.at(index++);
	}
};
}

TEST(SpellPointStateTest, SerializationRoundTripAndInvalidLoadAreAtomic)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 50, 100));
	PoolArchive saved{true};
	state.serialize(saved);
	SpellPointState restored;
	PoolArchive loaded{false, saved.values};
	restored.serialize(loaded);
	EXPECT_EQ(restored.getNormal(), 30);
	EXPECT_EQ(restored.getBuffer(), 50);
	for(const auto invalid : {std::array<int32_t, 2>{-1, 50}, std::array<int32_t, 2>{30, -1}})
	{
		PoolArchive bad{false, invalid};
		EXPECT_THROW(restored.serialize(bad), std::runtime_error);
		EXPECT_EQ(restored.getNormal(), 30);
		EXPECT_EQ(restored.getBuffer(), 50);
	}
}

TEST(SpellPointStateTest, ZeroAndExactPoolSpending)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 50, 100));
	ASSERT_TRUE(state.spend(0));
	EXPECT_EQ(state.getTotal(), 80);
	ASSERT_TRUE(state.spend(20));
	EXPECT_EQ(state.getNormal(), 30);
	EXPECT_EQ(state.getBuffer(), 30);
	ASSERT_TRUE(state.spend(30));
	EXPECT_EQ(state.getNormal(), 30);
	EXPECT_EQ(state.getBuffer(), 0);
	ASSERT_TRUE(state.spend(30));
	EXPECT_EQ(state.getTotal(), 0);
}

TEST(SpellPointStateTest, ReequippingCapacityDoesNotRestoreLostNormal)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(120, 50, 120));
	ASSERT_TRUE(state.restoreNormal(0, 100));
	state.clampNormal(120);
	EXPECT_EQ(state.getNormal(), 100);
	EXPECT_EQ(state.getBuffer(), 50);
}

TEST(SpellPointStateTest, SpendingFromWideTotalDoesNotOverflow)
{
	constexpr int32_t maximum = std::numeric_limits<int32_t>::max();
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(maximum, maximum, maximum));
	ASSERT_TRUE(state.spend(maximum));
	EXPECT_EQ(state.getNormal(), maximum);
	EXPECT_EQ(state.getBuffer(), 0);
	ASSERT_TRUE(state.spend(maximum));
	EXPECT_EQ(state.getTotal(), 0);
}

TEST(SpellPointStateTest, ReservoirGrantDoesNotRestoreNormalAndFullRefillPreservesBuffer)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 0, 50));
	ASSERT_TRUE(state.grantBuffer(50));
	EXPECT_EQ(state.getNormal(), 30);
	EXPECT_EQ(state.getBuffer(), 50);
	EXPECT_EQ(state.getTotal(), 80);

	ASSERT_TRUE(state.restoreNormal(50, 50));
	EXPECT_EQ(state.getNormal(), 50);
	EXPECT_EQ(state.getBuffer(), 50);
	EXPECT_EQ(state.getTotal(), 100);
}

TEST(SpellPointStateTest, RefillAndBufferGrantOrderProducesTheSamePools)
{
	SpellPointState refillFirst;
	SpellPointState bufferFirst;

	ASSERT_TRUE(refillFirst.restoreNormal(50, 50));
	ASSERT_TRUE(refillFirst.grantBuffer(25));
	ASSERT_TRUE(bufferFirst.grantBuffer(25));
	ASSERT_TRUE(bufferFirst.restoreNormal(50, 50));

	EXPECT_EQ(refillFirst.getNormal(), bufferFirst.getNormal());
	EXPECT_EQ(refillFirst.getBuffer(), bufferFirst.getBuffer());
	EXPECT_EQ(refillFirst.getTotal(), bufferFirst.getTotal());
	EXPECT_EQ(refillFirst.getNormal(), 50);
	EXPECT_EQ(refillFirst.getBuffer(), 25);
	EXPECT_EQ(refillFirst.getTotal(), 75);
}

TEST(SpellPointStateTest, CapacityIncreaseDoesNotFillNewNormalCapacity)
{
	SpellPointState state;
	state.setNormal(80, 100);

	state.clampNormal(120);

	EXPECT_EQ(state.getNormal(), 80);
	EXPECT_EQ(state.getTotal(), 80);
}

TEST(SpellPointStateTest, CapacityDecreaseClampsOnlyNormalAndSnapshotPreservesBuffer)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(120, 50, 120));

	state.clampNormal(100);

	EXPECT_EQ(state.getNormal(), 100);
	EXPECT_EQ(state.getBuffer(), 50);
	EXPECT_EQ(state.getTotal(), 150);

	SpellPointState restored;
	ASSERT_TRUE(restored.restoreSnapshot(120, 50, 100));
	EXPECT_EQ(restored.getNormal(), 100);
	EXPECT_EQ(restored.getBuffer(), 50);
	EXPECT_EQ(restored.getTotal(), 150);
}

TEST(SpellPointStateTest, SpendingConsumesBufferBeforeNormal)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 50, 50));

	ASSERT_TRUE(state.spend(60));
	EXPECT_EQ(state.getBuffer(), 0);
	EXPECT_EQ(state.getNormal(), 20);
	EXPECT_EQ(state.getTotal(), 20);
}

TEST(SpellPointStateTest, InvalidCostsAndNegativeGrantsAreAtomic)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 50, 50));

	EXPECT_FALSE(state.spend(81));
	EXPECT_FALSE(state.spend(-1));
	EXPECT_FALSE(state.restoreNormal(-1, 50));
	EXPECT_FALSE(state.grantBuffer(-1));

	EXPECT_EQ(state.getNormal(), 30);
	EXPECT_EQ(state.getBuffer(), 50);
	EXPECT_EQ(state.getTotal(), 80);
}

TEST(SpellPointStateTest, AdditionsSaturateEachPoolAndTotalUsesWideArithmetic)
{
	constexpr int32_t maximum = std::numeric_limits<int32_t>::max();
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(maximum - 3, maximum - 2, maximum));
	ASSERT_TRUE(state.restoreNormal(maximum, maximum));
	ASSERT_TRUE(state.grantBuffer(maximum));

	EXPECT_EQ(state.getNormal(), maximum);
	EXPECT_EQ(state.getBuffer(), maximum);
	EXPECT_EQ(state.getTotal(), static_cast<int64_t>(maximum) * 2);
}

TEST(SpellPointStateTest, NegativeMaximumNormalizesToZero)
{
	SpellPointState state;
	state.setNormal(10, -1);

	EXPECT_EQ(state.getNormal(), 0);
	EXPECT_EQ(state.getBuffer(), 0);
	EXPECT_EQ(state.getTotal(), 0);
}

TEST(SpellPointStateTest, InvalidSnapshotLeavesExistingPoolsUnchanged)
{
	SpellPointState state;
	ASSERT_TRUE(state.restoreSnapshot(30, 50, 50));

	EXPECT_FALSE(state.restoreSnapshot(-1, 20, 50));
	EXPECT_FALSE(state.restoreSnapshot(20, -1, 50));
	EXPECT_EQ(state.getNormal(), 30);
	EXPECT_EQ(state.getBuffer(), 50);
}
