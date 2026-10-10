/*
 * GrayscalePngPalette.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace grayscalePngPalette
{
using Color = std::array<uint8_t, 4>;

/// SDL3_image 3.4.4's libpng backend divides grayscale palette entries by
/// ncolors instead of ncolors - 1. Repair only that exact, identified output;
/// sprite palettes and authored RGB images must not be reinterpreted.
inline bool repair(std::span<const uint8_t> pngBytes, std::span<Color> palette)
{
	static constexpr std::array<uint8_t, 16> header = {
		137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'
	};
	if(pngBytes.size() < 33 || palette.size() != 256
		|| !std::equal(header.begin(), header.end(), pngBytes.begin()))
		return false;

	auto dimension = [&](std::size_t offset)
	{
		return (uint32_t(pngBytes[offset]) << 24) | (uint32_t(pngBytes[offset + 1]) << 16)
			| (uint32_t(pngBytes[offset + 2]) << 8) | uint32_t(pngBytes[offset + 3]);
	};
	const uint32_t width = dimension(16);
	const uint32_t height = dimension(20);
	if(width == 0 || height == 0 || width > 0x7fffffffU || height > 0x7fffffffU
		|| pngBytes[24] != 8 || pngBytes[25] != 0 || pngBytes[26] != 0
		|| pngBytes[27] != 0 || pngBytes[28] > 1)
		return false;

	for(std::size_t i = 0; i < palette.size(); ++i)
	{
		const uint8_t expected = static_cast<uint8_t>((i * 255) / palette.size());
		if(palette[i][0] != expected || palette[i][1] != expected || palette[i][2] != expected)
			return false;
	}

	for(std::size_t i = 0; i < palette.size(); ++i)
		palette[i][0] = palette[i][1] = palette[i][2] = static_cast<uint8_t>(i);
	return true;
}
}
