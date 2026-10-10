/*
 * GrayscalePngPaletteTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../clientsdl3/render/GrayscalePngPalette.h"

#include <gtest/gtest.h>

namespace
{
std::array<uint8_t, 33> grayscaleHeader()
{
	return {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
		0, 0, 0, 58, 0, 0, 0, 64, 8, 0, 0, 0, 0, 0, 0, 0, 0};
}

std::array<grayscalePngPalette::Color, 256> faultyPalette()
{
	std::array<grayscalePngPalette::Color, 256> colors;
	for(size_t i = 0; i < colors.size(); ++i)
	{
		const uint8_t gray = static_cast<uint8_t>(i * 255 / 256);
		colors[i] = {gray, gray, gray, static_cast<uint8_t>(i)};
	}
	return colors;
}
}

TEST(GrayscalePngPaletteTest, RepairsEveryEntryAndPreservesAlpha)
{
	auto colors = faultyPalette();
	ASSERT_TRUE(grayscalePngPalette::repair(grayscaleHeader(), colors));
	for(size_t i = 0; i < colors.size(); ++i)
		EXPECT_EQ(colors[i], (grayscalePngPalette::Color{static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i)}));
	const auto corrected = colors;
	EXPECT_FALSE(grayscalePngPalette::repair(grayscaleHeader(), colors));
	EXPECT_EQ(colors, corrected);
}

TEST(GrayscalePngPaletteTest, NonmatchingPaletteIsUnchanged)
{
	for(size_t index = 0; index < 256; ++index)
	{
		auto colors = faultyPalette();
		colors[index][1] ^= 1;
		const auto original = colors;
		EXPECT_FALSE(grayscalePngPalette::repair(grayscaleHeader(), colors));
		EXPECT_EQ(colors, original);
	}
}

TEST(GrayscalePngPaletteTest, RejectsEveryTruncatedHeaderAndWrongPaletteSize)
{
	const auto header = grayscaleHeader();
	auto colors = faultyPalette();
	const auto original = colors;
	for(size_t length = 0; length < header.size(); ++length)
		EXPECT_FALSE(grayscalePngPalette::repair({header.data(), length}, colors));
	EXPECT_FALSE(grayscalePngPalette::repair(header, {colors.data(), 255}));
	EXPECT_EQ(colors, original);
}

TEST(GrayscalePngPaletteTest, RejectsNonPngAndOtherPngFormats)
{
	for(const auto [offset, value] : std::array<std::pair<size_t, uint8_t>, 10>{{
		{0, 0}, {11, 12}, {12, 'X'}, {24, 16}, {25, 2}, {25, 3}, {25, 4}, {26, 1}, {27, 1}, {28, 2}}})
	{
		auto header = grayscaleHeader();
		header[offset] = value;
		auto colors = faultyPalette();
		const auto original = colors;
		EXPECT_FALSE(grayscalePngPalette::repair(header, colors));
		EXPECT_EQ(colors, original);
	}
}

TEST(GrayscalePngPaletteTest, RejectsInvalidDimensionsButAcceptsInterlacedGrayscale)
{
	for(const size_t offset : {16U, 20U})
	{
		auto header = grayscaleHeader();
		std::fill_n(header.begin() + offset, 4, 0);
		auto colors = faultyPalette();
		EXPECT_FALSE(grayscalePngPalette::repair(header, colors));
		header[offset] = 128;
		EXPECT_FALSE(grayscalePngPalette::repair(header, colors));
	}
	auto header = grayscaleHeader();
	header[28] = 1;
	auto colors = faultyPalette();
	EXPECT_TRUE(grayscalePngPalette::repair(header, colors));
}
