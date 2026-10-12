/* GPL-2.0-or-later. CPU-only SDL3 outline equivalence and component timing.
 * No SDL_Init, window, renderer, audio, events, resources or original assets.
 */
#include "StdInc.h"
#include "render/SDL_Extensions.h"
#include <SDL3/SDL_surface.h>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

[[noreturn]] void handleFatalError(const std::string & message, bool)
{
	throw std::runtime_error(message);
}

namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
void require(bool condition, const char * reason)
{
	if(!condition) throw std::runtime_error(reason);
}

struct Fixture
{
	std::vector<Uint32> storage;
	Surface image{nullptr, SDL_DestroySurface};
	Fixture(SDL_PixelFormat format, int width, int height, bool mirrored, bool dense = false, bool padded = true)
	{
		const int bytes = SDL_BYTESPERPIXEL(format);
		const int pitch = ((width * bytes + 3) / 4) * 4 + (padded ? 12 : 0);
		storage.assign(static_cast<size_t>(pitch) * height / 4, 0xa5a5a5a5);
		image.reset(SDL_CreateSurfaceFrom(width, height, format, storage.data(), pitch));
		require(bool(image), "fixture allocation");
		if(format == SDL_PIXELFORMAT_INDEX8)
		{
			auto * palette = SDL_CreateSurfacePalette(image.get());
			require(palette != nullptr, "fixture palette");
			std::array<SDL_Color, 256> colors{};
			for(int i = 0; i < 256; ++i)
				colors[i] = {Uint8(i), Uint8(255 - i), Uint8(i / 2), Uint8(i)};
			require(SDL_SetPaletteColors(palette, colors.data(), 0, 256), "fixture palette colors");
		}
		for(int y = 0; y < height; ++y)
			for(int x = 0; x < width; ++x)
			{
				const int logicalX = mirrored ? width - 1 - x : x;
				const bool occupied = dense || (width > 100
					? logicalX >= 160 && logicalX < 211 && y >= 180 && y < 233
					: ((logicalX >= 2 && logicalX < 6 && y >= 1 && y < 5) || (logicalX == 0 && y == 0)));
				const std::array<Uint8, 7> alphas{0, 1, 63, 64, 65, 254, 255};
				const Uint8 alpha = width < 100 && logicalX == 0 && y == 0 ? 255
					: occupied ? alphas[(logicalX + y) % alphas.size()] : 0;
				if(format == SDL_PIXELFORMAT_INDEX8)
					static_cast<Uint8 *>(image->pixels)[y * pitch + x] = alpha;
				else
					require(SDL_WriteSurfacePixel(image.get(), x, y, 33, 99, 177, alpha), "fixture pixel");
			}
		// A color key must not introduce transparency: old getRGBA ignores it.
		if(!SDL_ISPIXELFORMAT_FLOAT(format))
			require(SDL_SetSurfaceColorKey(image.get(), true, CSDL_Ext::getPixel(image.get(), 2, 2)), "fixture color key");
	}
};

// Independent old circular stencil.32bpp uses original generic getColor calls;
// other formats use generic SDL sampling, correcting the old undefined Uint32
// reinterpretation/tight-pitch assumption rather than certifying that assumption.
Surface reference(SDL_Surface * source, SDL_Color color, int thickness, Uint8 threshold, bool parallel = false)
{
	Surface result(SDL_CreateSurface(source->w, source->h, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
	require(bool(result), "reference allocation");
	const auto alphaAt = [&](int x, int y)
	{
		Uint8 alpha = 0;
		if(SDL_BYTESPERPIXEL(source->format) == 4 && !SDL_ISPIXELFORMAT_FLOAT(source->format))
		{
			Uint32 pixel;
			std::memcpy(&pixel, static_cast<const Uint8 *>(source->pixels) + y * source->pitch + x * 4, 4);
			alpha = CSDL_Ext::getColor(source, pixel).a;
		}
		else
			require(SDL_ReadSurfacePixel(source, x, y, nullptr, nullptr, nullptr, &alpha), "reference read");
		return alpha;
	};
	const auto rows = [&](const tbb::blocked_range<int> & range)
	{
	for(int y = range.begin(); y < range.end(); ++y)
		for(int x = 0; x < source->w; ++x)
		{
			const Uint8 original = alphaAt(x, y);
			if(threshold == 0 ? original != 0 : original >= threshold) continue;
			Uint8 maximum = 0;
			for(int dy = -thickness; dy <= thickness; ++dy)
				for(int dx = -thickness; dx <= thickness; ++dx)
				{
					if(dx * dx + dy * dy > thickness * thickness) continue;
					const int nx = x + dx, ny = y + dy;
					if(nx >= 0 && nx < source->w && ny >= 0 && ny < source->h)
						maximum = std::max(maximum, alphaAt(nx, ny));
				}
			const Uint8 output = threshold == 0 ? maximum : maximum >= threshold ? color.a : 0;
			if(output)
			{
				const Uint32 pixel = CSDL_Ext::mapColor(result.get(), {color.r, color.g, color.b, output});
				std::memcpy(static_cast<Uint8 *>(result->pixels) + y * result->pitch + x * 4, &pixel, 4);
			}
		}
	};
	if(parallel)
		tbb::parallel_for(tbb::blocked_range<int>(0, source->h), rows);
	else
		rows(tbb::blocked_range<int>(0, source->h));
	return result;
}

uint64_t digest(SDL_Surface * surface)
{
	uint64_t value = 14695981039346656037ULL;
	for(int y = 0; y < surface->h; ++y)
		for(int x = 0; x < surface->w * 4; ++x)
			value = (value ^ static_cast<const Uint8 *>(surface->pixels)[y * surface->pitch + x]) * 1099511628211ULL;
	return value;
}

void compare(SDL_Surface * left, SDL_Surface * right)
{
	require(left->w == right->w && left->h == right->h && left->format == right->format, "full virtual canvas differs");
	for(int y = 0; y < left->h; ++y)
		require(std::memcmp(static_cast<Uint8 *>(left->pixels) + y * left->pitch,
			static_cast<Uint8 *>(right->pixels) + y * right->pitch, left->w * 4) == 0, "outline pixels differ");
}
}

int main()
{
	try
	{
		const SDL_Color color{17, 203, 69, 121};
		require(CSDL_Ext::drawOutline(nullptr, color, 1) == nullptr, "null input guard");
		int cases = 0;
		for(const auto format : {SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_BGRA8888,
			SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_RGBA4444,
			SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_RGBA128_FLOAT})
			for(bool mirrored : {false, true})
			{
				Fixture fixture(format, 9, 7, mirrored);
				const auto before = fixture.storage;
				for(Uint8 threshold : {0, 64, 255})
					for(int thickness : {1, 2, 4})
					{
						auto expected = reference(fixture.image.get(), color, thickness, threshold);
						Surface actual(CSDL_Ext::drawOutline(fixture.image.get(), color, thickness, threshold), SDL_DestroySurface);
						require(bool(actual), "outline missing");
						compare(expected.get(), actual.get());
						require(before == fixture.storage, "source or pitch padding mutated");
						++cases;
					}
				require(CSDL_Ext::drawOutline(fixture.image.get(), color, 0) == nullptr, "zero thickness changed");
			}
		Surface empty(SDL_CreateSurface(9, 7, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		require(bool(empty), "empty fixture");
		for(Uint8 threshold : {0, 64, 255})
		{
			auto expected = reference(empty.get(), color, 2, threshold);
			Surface actual(CSDL_Ext::drawOutline(empty.get(), color, 2, threshold), SDL_DestroySurface);
			compare(expected.get(), actual.get());
			++cases;
		}
		Fixture colorAlphaFixture(SDL_PIXELFORMAT_ARGB8888, 9, 7, false);
		for(Uint8 colorAlpha : {0, 255})
			for(Uint8 threshold : {0, 64, 255})
			{
				const SDL_Color edgeColor{color.r, color.g, color.b, colorAlpha};
				auto expected = reference(colorAlphaFixture.image.get(), edgeColor, 2, threshold);
				Surface actual(CSDL_Ext::drawOutline(colorAlphaFixture.image.get(), edgeColor, 2, threshold), SDL_DestroySurface);
				compare(expected.get(), actual.get());
				++cases;
			}
		for(bool dense : {false, true})
			for(bool singleThread : {true, false})
			{
				std::unique_ptr<tbb::global_control> control;
				if(singleThread)
					control = std::make_unique<tbb::global_control>(tbb::global_control::max_allowed_parallelism, 1);
				Fixture fixture(SDL_PIXELFORMAT_ARGB8888, 450, 400, false, dense, false);
				const auto before = fixture.storage;
				std::optional<uint64_t> repeatedDigest;
				for(int repeat = 0; repeat < 3; ++repeat)
				{
				const auto begin = std::chrono::steady_clock::now();
				auto expected = reference(fixture.image.get(), color, 2, 64, true);
				const auto middle = std::chrono::steady_clock::now();
				Surface actual(CSDL_Ext::drawOutline(fixture.image.get(), color, 2, 64), SDL_DestroySurface);
				const auto end = std::chrono::steady_clock::now();
				compare(expected.get(), actual.get());
				require(before == fixture.storage, "benchmark input mutated");
				if(repeatedDigest) require(*repeatedDigest == digest(actual.get()), "repeat output changed");
				repeatedDigest = digest(actual.get());
				std::cout << "outline_cpu dense=" << dense << " repeat=" << repeat
					<< " single_thread=" << singleThread
					<< " width=450 height=400 reference_us=" << std::chrono::duration_cast<std::chrono::microseconds>(middle - begin).count()
					<< " optimized_us=" << std::chrono::duration_cast<std::chrono::microseconds>(end - middle).count()
					<< " visible_pixel_digest=" << digest(actual.get()) << '\n';
				}
			}
		std::cout << "PASS: " << cases << " pitch/format/threshold/thickness/mirror cases; same-input sparse/dense CPU comparisons\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
