/*
 * OutlineMaskTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../render/OutlineMask.h"

#include <array>
#include <cassert>
#include <iostream>
#include <iterator>
#include <random>
#include <vector>

namespace
{
uint8_t legacyAlpha(const std::vector<uint8_t> & source, int x, int y, int width, int height, int thickness)
{
	if(source[y * width + x] != 0)
		return 0;
	uint8_t result = 0;
	for(int dy = -thickness; dy <= thickness; ++dy)
	{
		for(int dx = -thickness; dx <= thickness; ++dx)
		{
			if(dx * dx + dy * dy > thickness * thickness)
				continue;
			const int nx = x + dx;
			const int ny = y + dy;
			if(nx < 0 || ny < 0 || nx >= width || ny >= height)
				continue;
			result = std::max(result, source[ny * width + nx]);
		}
	}
	return result;
}

void checkMask()
{
	constexpr int width = 7;
	constexpr int height = 5;
	std::vector<uint8_t> image(width * height, 0);
	image[2 * width + 3] = 128;
	image[2 * width + 2] = 7;
	const auto unchanged = image;
	auto getAlpha = [&](int x, int y) { return image[y * width + x]; };
	assert(outlineMask::pixelAlpha(2, 2, width, height, 1, 0, 255, getAlpha) == 0);
	assert(outlineMask::pixelAlpha(1, 2, width, height, 1, 0, 255, getAlpha) == 7);
	assert(outlineMask::pixelAlpha(2, 2, width, height, 1, 64, 255, getAlpha) == 255);
	assert(outlineMask::pixelAlpha(1, 2, width, height, 1, 64, 255, getAlpha) == 0);
	assert(outlineMask::pixelAlpha(3, 2, width, height, 1, 64, 255, getAlpha) == 0);
	assert(outlineMask::pixelAlpha(2, 1, width, height, 1, 64, 255, getAlpha) == 0);
	assert(outlineMask::pixelAlpha(3, 1, width, height, 1, 128, 200, getAlpha) == 200);
	assert(outlineMask::pixelAlpha(3, 1, width, height, 1, 129, 255, getAlpha) == 0);
	assert(image == unchanged);
	// Compare every pixel, including corners, with the previous algorithm.
	std::mt19937 random(291);
	for(int sample = 0; sample < 100; ++sample)
	{
		for(auto & alpha : image)
			alpha = random() % 2 ? 0 : random() % 256;
		for(int thickness = 1; thickness <= 3; ++thickness)
		{
			for(int y = 0; y < height; ++y)
			{
				for(int x = 0; x < width; ++x)
					assert(outlineMask::pixelAlpha(x, y, width, height, thickness, 0, 255, getAlpha)
						== legacyAlpha(image, x, y, width, height, thickness));
			}
		}
	}
}
}

int main(int argc, char ** argv)
{
	checkMask();
	if(argc == 4)
	{
		const int width = std::stoi(argv[1]);
		const int height = std::stoi(argv[2]);
		const int threshold = std::stoi(argv[3]);
		const std::vector<uint8_t> image(std::istreambuf_iterator<char>(std::cin), {});
		assert(width > 0 && height > 0 && threshold >= 0 && threshold <= 255);
		assert(image.size() == static_cast<size_t>(width * height));
		auto getAlpha = [&](int x, int y) { return image[y * width + x]; };
		for(int y = 0; y < height; ++y)
		{
			for(int x = 0; x < width; ++x)
				std::cout.put(static_cast<char>(outlineMask::pixelAlpha(x, y, width, height, 1, threshold, 255, getAlpha)));
		}
	}
	else
		std::cout << "Outline mask and legacy compatibility checks passed.\n";
}
