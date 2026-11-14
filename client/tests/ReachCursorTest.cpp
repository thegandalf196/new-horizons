/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../render/ReachCursor.h"
#include "../render/ICursor.h"
#include "../../lib/Point.h"

#include <algorithm>
#include <limits>

namespace
{
reachCursor::NumberMetrics nativeMetrics()
{
	return {{{5, 5, 5, 5, 5, 5, 5, 5, 5, 5}}, 11};
}

int compositionCount = 0;
reachCursor::Frame countedComposition(const reachCursor::Key & key)
{
	++compositionCount;
	return {nullptr, reachCursor::makeBody(key.direction).hotspot};
}

class CursorProbe final : public ICursor
{
public:
	int imagesSet = 0;
	Point pivot;
	void setImage(std::shared_ptr<IImage>, const Point & hotspot) override { ++imagesSet; pivot = hotspot; }
	void setCursorPosition(const Point &) override {}
	void render() override {}
	void update() override {}
	void setVisible(bool) override {}
};
}

TEST(ReachCursorTest, OnlyPositiveExtendedReachAndRealDirectionsAreAccepted)
{
	using namespace reachCursor;
	EXPECT_FALSE(makeLayout({Direction::EAST, 1}, nativeMetrics()));
	EXPECT_FALSE(makeLayout({Direction::EAST, -1}, nativeMetrics()));
	EXPECT_FALSE(makeLayout({static_cast<Direction>(8), 6}, nativeMetrics()));
	EXPECT_TRUE(makeLayout({Direction::EAST, 2}, nativeMetrics()));
	EXPECT_TRUE(makeLayout({Direction::EAST, 6}, nativeMetrics()));
}

TEST(ReachCursorTest, EveryDirectionKeepsAnOpaqueActualLanceTipHotspot)
{
	using namespace reachCursor;
	const std::array<PixelPoint, 8> expected{{{25,5},{29,15},{25,25},{5,25},{1,15},{5,5},{15,1},{15,29}}};
	for(size_t direction = 0; direction < expected.size(); ++direction)
	{
		const auto layout = makeLayout({static_cast<Direction>(direction), 6}, nativeMetrics());
		ASSERT_TRUE(layout);
		EXPECT_EQ(layout->hotspot, expected[direction]);
		EXPECT_EQ(layout->bodyOffset, PixelPoint{});
		const auto pixel = layout->body.pixels[layout->hotspot.y * Body::SIZE + layout->hotspot.x];
		ASSERT_GT(pixel, 0);
		ASSERT_LT(pixel, palette().size());
		EXPECT_EQ(palette()[pixel].a, 255);
		EXPECT_GT(std::count(layout->body.pixels.begin(), layout->body.pixels.end(), 3), 0);
		EXPECT_GT(std::count(layout->body.pixels.begin(), layout->body.pixels.end(), 6), 0);
		EXPECT_LE(layout->size.x, 64);
		EXPECT_LE(layout->size.y, 64);
	}
}

TEST(ReachCursorTest, NumberIsExactCurrentMaximumAndStaysBesideTheLance)
{
	using namespace reachCursor;
	for(const int64_t maximum : {2, 6, 8, 12, 99})
	{
		const auto layout = makeLayout({Direction::EAST, maximum}, nativeMetrics());
		ASSERT_TRUE(layout);
		ASSERT_EQ(layout->number.size(), 1);
		EXPECT_EQ(layout->number.front().text, std::to_string(maximum));
		EXPECT_GT(layout->number.front().position.x - 1, layout->body.inkBottomRight.x);
		EXPECT_LT(layout->number.front().position.x + 5 * layout->number.front().text.size(), layout->size.x);
	}
}

TEST(ReachCursorTest, ArbitraryInt64MaximumIsCompleteAndTranslatesItsTrueTip)
{
	using namespace reachCursor;
	const auto maximum = std::numeric_limits<int64_t>::max();
	const auto layout = makeLayout({Direction::NORTH_EAST, maximum}, nativeMetrics());
	ASSERT_TRUE(layout);
	ASSERT_EQ(layout->number.size(), 2);
	std::string displayed;
	for(const auto & row : layout->number)
	{
		displayed += row.text;
		EXPECT_LE(row.text.size(), 10);
		EXPECT_GT(row.position.y, layout->body.inkBottomRight.y);
		EXPECT_GT(row.position.x, 0);
	}
	EXPECT_EQ(displayed, std::to_string(maximum));
	EXPECT_EQ(layout->size, (PixelPoint{54, 56}));
	EXPECT_EQ(layout->hotspot, (PixelPoint{36, 5}));
	EXPECT_EQ(layout->bodyOffset, (PixelPoint{11, 0}));
}

TEST(ReachCursorTest, RangeAndDirectionChangesSelectDistinctCachedCompositions)
{
	using namespace reachCursor;
	Cache cache;
	compositionCount = 0;
	const auto * six = cache.get({Direction::EAST, 6}, countedComposition);
	ASSERT_NE(six, nullptr);
	EXPECT_EQ(cache.get({Direction::EAST, 6}, countedComposition), six);
	EXPECT_EQ(compositionCount, 1);
	EXPECT_NE(cache.get({Direction::EAST, 8}, countedComposition), six);
	EXPECT_NE(cache.get({Direction::NORTH, 6}, countedComposition), six);
	EXPECT_EQ(compositionCount, 3);
	EXPECT_EQ(cache.get({Direction::EAST, 6}, countedComposition), six);
	EXPECT_EQ(cache.get({Direction::EAST, 1}, countedComposition), nullptr);
	EXPECT_EQ(compositionCount, 3);
	cache.clear();
	EXPECT_EQ(cache.size(), 0);
	EXPECT_NE(cache.get({Direction::EAST, 6}, countedComposition), nullptr);
	EXPECT_EQ(compositionCount, 4);
}

TEST(ReachCursorTest, ComposedImageCacheHasAFixedMemoryEntryBound)
{
	using namespace reachCursor;
	Cache cache;
	compositionCount = 0;
	for(int64_t maximum = 2; maximum < static_cast<int64_t>(Cache::CAPACITY) + 4; ++maximum)
	{
		ASSERT_NE(cache.get({Direction::WEST, maximum}, countedComposition), nullptr);
		EXPECT_LE(cache.size(), Cache::CAPACITY);
	}
	EXPECT_EQ(compositionCount, Cache::CAPACITY + 2);
}

TEST(ReachCursorTest, ProductionHandoffUsesPhysicalHotspotsAndPreservesLogicalCoordinates)
{
	using namespace reachCursor;
	const std::array<PixelPoint, 8> native{{{25,5},{29,15},{25,25},{5,25},{1,15},{5,5},{15,1},{15,29}}};
	const std::array<PixelPoint, 8> twice{{{50,10},{58,30},{50,50},{10,50},{2,30},{10,10},{30,2},{30,58}}};
	for(int direction = 0; direction < 8; ++direction)
		for(const int64_t maximum : {int64_t{6}, std::numeric_limits<int64_t>::max()})
		{
			const auto layout = makeLayout({static_cast<Direction>(direction), maximum}, nativeMetrics());
			ASSERT_TRUE(layout);
			// This device probe observes the production handoff arguments without
			// initializing SDL; raster appearance still requires rendered tests.
			const Frame frame{nullptr, layout->hotspot};
			for(int scaling : {1, 2})
			{
				CursorProbe backend;
				frame.apply(backend, scaling);
				EXPECT_EQ(backend.imagesSet, 1);
				const auto expected = scaling == 1 ? native[direction] : twice[direction];
				const int wideNumberOffset = maximum == 6 ? 0 : scaling == 1 ? 11 : 22;
				EXPECT_EQ(backend.pivot.x, expected.x + wideNumberOffset);
				EXPECT_EQ(backend.pivot.y, expected.y);
				EXPECT_EQ(frame.hotspot, layout->hotspot);
			}
		}
}
