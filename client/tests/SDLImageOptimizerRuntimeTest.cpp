/* GPL-2.0-or-later. Synthetic SDL3 surface optimizer equivalence regression.
 * No SDL_Init, window, renderer, audio, events or resource loading.
 */
#include "StdInc.h"
#include "render/SDLImageScaler.h"
#include "render/SDL_Extensions.h"
#include "lib/Rect.h"

#include <SDL3/SDL_surface.h>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

[[noreturn]] void handleFatalError(const std::string & message, bool)
{
	throw std::runtime_error(message);
}

namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

struct Fixture
{
	// Aligned external storage, deliberately padded and not owned by SDL.
	std::vector<uint32_t> storage;
	Surface surface{nullptr, SDL_DestroySurface};
	Fixture(SDL_PixelFormat format, int width = 9, int height = 7)
	{
		const int bytes = SDL_BYTESPERPIXEL(format);
		const int pitch = ((width * bytes + 3) / 4) * 4 + 12;
		storage.assign(pitch * height / 4, 0xa5a5a5a5u);
		surface.reset(SDL_CreateSurfaceFrom(width, height, format, storage.data(), pitch));
		require(bool(surface), "padded surface allocation failed");
		for(int y = 0; y < height; ++y)
			std::memset(static_cast<uint8_t *>(surface->pixels) + y * pitch, 0, width * bytes);
		if(format == SDL_PIXELFORMAT_INDEX8)
		{
			auto * palette = SDL_CreateSurfacePalette(surface.get());
			require(palette != nullptr, "palette allocation failed");
			std::array<SDL_Color, 256> colors{};
			for(size_t i = 0; i < colors.size(); ++i)
				colors[i] = SDL_Color{uint8_t(i), uint8_t(255 - i), uint8_t(i / 2), 255};
			colors[5].a = 0; // Nonzero transparent index still contributes to old bounds.
			require(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()), "palette update failed");
		}
	}
};

void pixel(SDL_Surface * surface, int x, int y, uint8_t alpha)
{
	const auto * format = SDL_GetPixelFormatDetails(surface->format);
	const uint32_t value = SDL_MapRGBA(format, nullptr, 91, 37, 173, alpha);
	auto * address = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + x * format->bytes_per_pixel;
	if(format->bytes_per_pixel == 3)
	{
		address[0] = value;
		address[1] = value >> 8;
		address[2] = value >> 16;
	}
	else if(format->bytes_per_pixel == 2)
	{
		const uint16_t narrow = value;
		std::memcpy(address, &narrow, sizeof(narrow));
	}
	else
		std::memcpy(address, &value, sizeof(value));
}

struct Bounds
{
	int left, top, right = 0, bottom = 0;
	bool empty(int width) const { return left == width; }
};

// Faithful old generic scan: getPixel + SDL_GetRGBA for every nonindexed pixel.
// Indexed legacy semantics depend on the index, NOT palette alpha/color key.
Bounds referenceBounds(SDL_Surface * surface)
{
	Bounds bounds{surface->w, surface->h};
	const bool indexed = CSDL_Ext::getPalette(surface) != nullptr;
	const auto * format = CSDL_Ext::getFormat(surface);
	for(int y = 0; y < surface->h; ++y)
		for(int x = 0; x < surface->w; ++x)
		{
			const auto value = CSDL_Ext::getPixel(surface, x, y);
			uint8_t alpha = 0;
			if(!indexed)
				SDL_GetRGBA(value, format, nullptr, nullptr, nullptr, nullptr, &alpha);
			if(indexed ? value != 0 : alpha != SDL_ALPHA_TRANSPARENT)
			{
				bounds.left = std::min(bounds.left, x);
				bounds.top = std::min(bounds.top, y);
				bounds.right = std::max(bounds.right, x);
				bounds.bottom = std::max(bounds.bottom, y);
			}
		}
	return bounds;
}

Surface referenceOptimize(SDL_Surface * input, SDL_Surface * formatSource, Rect & dimensions)
{
	if(!input)
		return Surface(nullptr, SDL_DestroySurface);
	const auto bounds = referenceBounds(input);
	if(bounds.empty(input->w))
		return Surface(nullptr, SDL_DestroySurface);
	if(bounds.left == 0 && bounds.top == 0 && bounds.right == input->w - 1 && bounds.bottom == input->h - 1)
	{
		++input->refcount;
		return Surface(input, SDL_DestroySurface);
	}
	const Rect crop(bounds.left, bounds.top, bounds.right - bounds.left + 1, bounds.bottom - bounds.top + 1);
	SDL_Rect source = CSDL_Ext::toSDL(crop);
	Surface result(CSDL_Ext::newSurface(crop.dimensions(), formatSource), SDL_DestroySurface);
	require(SDL_SetSurfaceBlendMode(input, SDL_BLENDMODE_NONE), "reference blend update failed");
	require(SDL_BlitSurface(input, &source, result.get(), nullptr), "reference crop blit failed");
	if(SDL_SurfaceHasColorKey(input))
	{
		uint32_t key;
		require(SDL_GetSurfaceColorKey(input, &key), "reference color key read failed");
		require(SDL_SetSurfaceColorKey(result.get(), true, key), "reference color key copy failed");
	}
	dimensions.x += bounds.left;
	dimensions.y += bounds.top;
	return result;
}

void compare(Fixture & actual, Fixture & reference, SDL_Surface * actualFormat = nullptr, SDL_Surface * referenceFormat = nullptr)
{
	require(actual.storage == reference.storage, "fixture bytes differ before optimization");
	const auto originalBytes = actual.storage;
	const int originalRefcount = actual.surface->refcount;
	Rect expectedDimensions(11, 13, 31, 37);
	auto expected = referenceOptimize(reference.surface.get(), referenceFormat ? referenceFormat : reference.surface.get(), expectedDimensions);
	SDLImageOptimizer optimizer(actual.surface.get(), Rect(11, 13, 31, 37));
	optimizer.optimizeSurface(actualFormat ? actualFormat : actual.surface.get());
	Surface result(optimizer.acquireResultSurface(), SDL_DestroySurface);
	require(optimizer.acquireResultSurface() == nullptr, "result ownership was not transferred exactly once");
	const auto & dimensions = optimizer.getResultDimensions();
	require(dimensions.x == expectedDimensions.x && dimensions.y == expectedDimensions.y
		&& dimensions.w == expectedDimensions.w && dimensions.h == expectedDimensions.h, "virtual geometry mismatch");
	require(bool(result) == bool(expected), "empty result mismatch");
	if(result)
	{
		require((result.get() == actual.surface.get()) == (expected.get() == reference.surface.get()), "alias ownership mismatch");
		require(result->format == expected->format && result->w == expected->w && result->h == expected->h, "output format/geometry mismatch");
		const int rowBytes = result->w * SDL_BYTESPERPIXEL(result->format);
		for(int y = 0; y < result->h; ++y)
			require(std::memcmp(static_cast<uint8_t *>(result->pixels) + y * result->pitch,
				static_cast<uint8_t *>(expected->pixels) + y * expected->pitch, rowBytes) == 0, "output pixel bytes mismatch");
		const auto * palette = CSDL_Ext::getPalette(result.get());
		const auto * expectedPalette = CSDL_Ext::getPalette(expected.get());
		require(bool(palette) == bool(expectedPalette), "output palette presence mismatch");
		if(palette)
		{
			require(palette->ncolors == expectedPalette->ncolors, "palette size mismatch");
			require(std::memcmp(palette->colors, expectedPalette->colors, palette->ncolors * sizeof(SDL_Color)) == 0, "palette bytes mismatch");
		}
		require(SDL_SurfaceHasColorKey(result.get()) == SDL_SurfaceHasColorKey(expected.get()), "color key presence mismatch");
		if(SDL_SurfaceHasColorKey(result.get()))
		{
			uint32_t key, expectedKey;
			require(SDL_GetSurfaceColorKey(result.get(), &key) && SDL_GetSurfaceColorKey(expected.get(), &expectedKey), "color key read failed");
			require(key == expectedKey, "color key mismatch");
		}
	}
	require(actual.surface->refcount == originalRefcount + (result.get() == actual.surface.get()), "input reference count mismatch");
	result.reset();
	require(actual.surface->refcount == originalRefcount, "destroying result did not preserve input ownership");
	require(actual.storage == originalBytes, "input pixels or pitch padding mutated");
}

void runCases()
{
	SDLImageOptimizer nullOptimizer(nullptr, Rect(1, 2, 3, 4));
	nullOptimizer.optimizeSurface(nullptr);
	const auto & nullDimensions = nullOptimizer.getResultDimensions();
	require(nullOptimizer.acquireResultSurface() == nullptr && nullOptimizer.acquireResultSurface() == nullptr
		&& nullDimensions.x == 1 && nullDimensions.y == 2 && nullDimensions.w == 3 && nullDimensions.h == 4, "null input contract failed");
	unsigned int cases = 1;
	for(const auto format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_BGRA8888, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_ARGB2101010, SDL_PIXELFORMAT_RGB24})
		for(const auto alpha : {0, 1, 128, 255})
			for(int pattern = 0; pattern < 3; ++pattern)
			{
				Fixture actual(format), reference(format);
				for(int y = 0; y < actual.surface->h; ++y)
					for(int x = 0; x < actual.surface->w; ++x)
					{
						// Nonzero RGB under transparent alpha is not an opaque pixel.
						pixel(actual.surface.get(), x, y, 0);
						pixel(reference.surface.get(), x, y, 0);
					}
				const std::vector<Point> points = pattern == 0 ? std::vector<Point>{Point(4, 3)}
					: pattern == 1 ? std::vector<Point>{Point(2, 1), Point(6, 5)}
					: std::vector<Point>{Point(0, 0), Point(8, 6)};
				for(const auto & point : points)
				{
					pixel(actual.surface.get(), point.x, point.y, alpha);
					pixel(reference.surface.get(), point.x, point.y, alpha);
				}
				compare(actual, reference);
				++cases;
			}
	for(int pattern = 0; pattern < 3; ++pattern)
	{
		Fixture actual(SDL_PIXELFORMAT_INDEX8), reference(SDL_PIXELFORMAT_INDEX8);
		for(auto * surface : {actual.surface.get(), reference.surface.get()})
		{
			require(SDL_SetSurfaceColorKey(surface, true, 7), "fixture color key failed");
			if(pattern != 0)
			{
				static_cast<uint8_t *>(surface->pixels)[surface->pitch + 2] = 5;
				static_cast<uint8_t *>(surface->pixels)[5 * surface->pitch + 6] = 7;
			}
			if(pattern == 2)
			{
				static_cast<uint8_t *>(surface->pixels)[0] = 5;
				static_cast<uint8_t *>(surface->pixels)[6 * surface->pitch + 8] = 8;
			}
		}
		compare(actual, reference);
		++cases;
	}
	Fixture actual(SDL_PIXELFORMAT_ARGB8888), reference(SDL_PIXELFORMAT_ARGB8888);
	Fixture actualFormat(SDL_PIXELFORMAT_ABGR8888), referenceFormat(SDL_PIXELFORMAT_ABGR8888);
	pixel(actual.surface.get(), 3, 2, 128);
	pixel(reference.surface.get(), 3, 2, 128);
	compare(actual, reference, actualFormat.surface.get(), referenceFormat.surface.get());
	++cases;
	// Result must also remain usable when the caller releases the input first.
	for(bool full : {false, true})
	{
		Surface input(SDL_CreateSurface(9, 7, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		require(bool(input), "owned lifetime input allocation failed");
		require(SDL_FillSurfaceRect(input.get(), nullptr, 0), "owned lifetime input clear failed");
		pixel(input.get(), full ? 0 : 3, full ? 0 : 2, 255);
		if(full)
			pixel(input.get(), 8, 6, 128);
		SDLImageOptimizer optimizer(input.get(), Rect(0, 0, 9, 7));
		optimizer.optimizeSurface(input.get());
		Surface result(optimizer.acquireResultSurface(), SDL_DestroySurface);
		require(result && (result.get() == input.get()) == full, "lifetime alias mismatch");
		const auto first = CSDL_Ext::getPixel(result.get(), 0, 0);
		input.reset();
		require(result->refcount == 1 && CSDL_Ext::getPixel(result.get(), 0, 0) == first, "result did not survive input release");
		++cases;
	}
	std::cout << "PASS " << cases << " optimizer reference/ownership/padded-pitch cases\n";
}

void benchmark()
{
	using Clock = std::chrono::steady_clock;
	long long scanUs = 0, referenceTotalUs = 0, actualTotalUs = 0;
	int checksum = 0;
	constexpr int iterations = 20;
	for(int iteration = 0; iteration < iterations; ++iteration)
	{
		Fixture actual(SDL_PIXELFORMAT_ARGB8888, 450, 400), reference(SDL_PIXELFORMAT_ARGB8888, 450, 400);
		for(int y = 7; y < 393; ++y)
			for(int x = 5; x < 445; ++x)
				if((x + y) % 5 == 0)
				{
					pixel(actual.surface.get(), x, y, 128);
					pixel(reference.surface.get(), x, y, 128);
				}
		// Setup/copies excluded from both optimizer totals; both include scan+crop.
		auto start = Clock::now();
		const auto bounds = referenceBounds(reference.surface.get());
		scanUs += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
		checksum += bounds.left + bounds.right;
		Rect dimensions(0, 0, 450, 400);
		start = Clock::now();
		auto oldResult = referenceOptimize(reference.surface.get(), reference.surface.get(), dimensions);
		referenceTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
		SDLImageOptimizer optimizer(actual.surface.get(), Rect(0, 0, 450, 400));
		start = Clock::now();
		optimizer.optimizeSurface(actual.surface.get());
		Surface result(optimizer.acquireResultSurface(), SDL_DestroySurface);
		actualTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
		require(result && oldResult && result->w == oldResult->w && result->h == oldResult->h, "benchmark geometry differs");
		for(int y = 0; y < result->h; ++y)
			require(std::memcmp(static_cast<uint8_t *>(result->pixels) + y * result->pitch,
				static_cast<uint8_t *>(oldResult->pixels) + y * oldResult->pitch, result->w * 4) == 0, "benchmark bytes differ");
	}
	std::cout << "BENCHMARK 450x400 ARGB8888 padded sparse, iterations=" << iterations
		<< " oldScanOnlyUs=" << scanUs << " oldOptimizerTotalUs=" << referenceTotalUs
		<< " actualOptimizerTotalUs=" << actualTotalUs << " checksum=" << checksum
		<< " (steady wall, allocation/blit included in totals; no FPS/GPU/cold-cache claim)\n";
}
}

int main(int argc, char ** argv)
{
	try
	{
		require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--benchmark"), "only optional --benchmark accepted");
		runCases();
		if(argc == 2)
			benchmark();
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
