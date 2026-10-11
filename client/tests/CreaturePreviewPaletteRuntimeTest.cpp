/* GPL-2.0-or-later. Surface-only creature preview equivalence regression.
 * No SDL_Init, window, renderer, audio, events or resource extraction.
 */
#include "StdInc.h"
#include "render/ScalableImage.h"
#include "render/SDLImage.h"
#include "render/ColorFilter.h"
#include "render/Colors.h"
#include "render/Graphics.h"
#include "lib/Rect.h"
#include <SDL3/SDL_surface.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

[[noreturn]] void handleFatalError(const std::string & message, bool)
{
	throw std::runtime_error(message);
}

namespace
{
void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

bool sameColor(const SDL_Color & a, const SDL_Color & b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void samePalette(const ScalableImageParameters & old, const ScalableImageParameters & preview)
{
	require(old.palette && preview.palette, "missing indexed palette");
	for(int i = 0; i < 256; ++i)
		require(sameColor(old.palette->colors[i], preview.palette->colors[i]), "palette mismatch");
}

void comparePixels(SDL_Surface * input, ScalableImageParameters & old, ScalableImageParameters & preview, int scale, bool clipped = false, int flip = 0)
{
	std::shared_ptr<const ISharedImage> image = std::make_shared<SDLImageShared>(input);
	if(flip & 1) image = image->horizontalFlip();
	if(flip & 2) image = image->verticalFlip();
	SDL_Surface * before = SDL_CreateSurface(input->w * scale, input->h * scale, SDL_PIXELFORMAT_RGB24);
	SDL_Surface * after = SDL_CreateSurface(input->w * scale, input->h * scale, SDL_PIXELFORMAT_RGB24);
	require(before && after, "output allocation failed");
	require(SDL_FillSurfaceRect(before, nullptr, 0x304050), "background fill failed");
	require(SDL_FillSurfaceRect(after, nullptr, 0x304050), "background fill failed");
	const auto * initialPixels = static_cast<const uint8_t *>(before->pixels);
	const std::vector<uint8_t> background(initialPixels, initialPixels + before->pitch * before->h);
	const Rect source(2 * scale, 3 * scale, 10 * scale, 9 * scale);
	const Rect * crop = clipped ? &source : nullptr;
	if(scale == 1)
	{
		image->draw(before, old.palette, Point(0, 0), crop, Colors::WHITE_TRUE, 255, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		image->draw(after, preview.palette, Point(0, 0), crop, Colors::WHITE_TRUE, 255, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION);
	}
	else
	{
		image->scaledDraw(before, old.palette, Point(before->w, before->h), Point(0, 0), crop, Colors::WHITE_TRUE, 255, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		image->scaledDraw(after, preview.palette, Point(after->w, after->h), Point(0, 0), crop, Colors::WHITE_TRUE, 255, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION);
	}
	bool drewPixels = false;
	for(int row = 0; row < before->h; ++row)
	{
		require(std::memcmp(static_cast<uint8_t *>(before->pixels) + row * before->pitch,
			static_cast<uint8_t *>(after->pixels) + row * after->pitch, before->w * 3) == 0, "surface pixel mismatch");
		drewPixels |= std::memcmp(static_cast<uint8_t *>(before->pixels) + row * before->pitch,
			background.data() + row * before->pitch, before->w * 3) != 0;
	}
	require(drewPixels, "both draw paths left the background unchanged");
	SDL_DestroySurface(before);
	SDL_DestroySurface(after);
}

void runIndexed(bool matching2, bool matching3)
{
	SDL_Palette * original = SDL_CreatePalette(256);
	require(original != nullptr, "palette allocation failed");
	std::array<SDL_Color, 256> colors{};
	for(size_t i = 0; i < colors.size(); ++i)
		colors[i] = SDL_Color{uint8_t(i), uint8_t(255-i), uint8_t(i/2), 255};
	const std::array<SDL_Color, 8> reserved = {{
		{0,255,255,255}, {255,150,255,255}, {255,100,255,255}, {255,50,255,255},
		{255,0,255,255}, {255,255,0,255}, {180,0,255,255}, {0,255,0,255}
	}};
	std::copy(reserved.begin(), reserved.end(), colors.begin());
	if(!matching2) colors[2] = SDL_Color{17,35,53,255};
	if(!matching3) colors[3] = SDL_Color{71,89,107,255};
	SDL_SetPaletteColors(original, colors.data(), 0, 256);
	{
		ScalableImageParameters old(original, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		old.setOverlayColor(original, Colors::TRANSPARENCY, true);
		ScalableImageParameters preview(original, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION);
		ScalableImageParameters battle(original, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		samePalette(old, preview);
		require(preview.palette->colors[0].a == 0 && preview.palette->colors[1].a == 64
			&& preview.palette->colors[4].a == 128 && preview.palette->colors[5].a == 0
			&& preview.palette->colors[6].a == 127 && preview.palette->colors[7].a == 63, "shadow/selection alpha changed");
		require(matching2 ? preview.palette->colors[2].a == 64 : sameColor(preview.palette->colors[2], colors[2]), "conditional2 changed");
		require(matching3 ? preview.palette->colors[3].a == 128 : sameColor(preview.palette->colors[3], colors[3]), "conditional3 changed");
		require(battle.palette->colors[5].a == 255, "battle selection lost");
		SDL_Surface * input = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_INDEX8);
		require(input != nullptr, "indexed allocation failed");
		SDL_SetSurfacePalette(input, original);
		for(int y = 0; y < 16; ++y)
			for(int x = 0; x < 16; ++x)
				static_cast<uint8_t *>(input->pixels)[y * input->pitch + x] = uint8_t(y * 16 + x);
		comparePixels(input, old, preview, 1);
		comparePixels(input, old, preview, 2);
		for(int flip = 0; flip < 4; ++flip)
		{
			comparePixels(input, old, preview, 1, true, flip);
			comparePixels(input, old, preview, 2, true, flip);
		}
		old.shiftPalette(original, 8, 32, 7);
		preview.shiftPalette(original, 8, 32, 7);
		samePalette(old, preview);
		for(int i = 0; i < 32; ++i)
			require(sameColor(preview.palette->colors[8+(i+7)%32], colors[8+i]), "palette shift changed");
		const auto grayscale = ColorFilter::genMuxerShifter({.299,.587,.114,0}, {.299,.587,.114,0}, {.299,.587,.114,0}, 1);
		const auto tint = ColorFilter::genRangeShifter(0, 0, 0, .5f, .8f, .3f);
		const auto effect = ColorFilter::genInterpolated(ColorFilter::genEmptyShifter(), ColorFilter::genCombined(grayscale, tint), .6f);
		old.adjustPalette(original, EImageBlitMode::WITH_SHADOW_AND_SELECTION, effect, 0);
		preview.adjustPalette(original, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION, effect, 0);
		samePalette(old, preview);
		comparePixels(input, old, preview, 1);
		comparePixels(input, old, preview, 2);
		SDL_DestroySurface(input);
	}
	SDL_DestroyPalette(original);
}
}

// Resource-backed hook: call from the maintained runtime fixture AFTER its actual
// Graphics initialization. Not called by the resource-free main; no fake Graphics.
void verifyCreaturePreviewPlayerTransforms(const SDL_Palette * original)
{
	require(graphics != nullptr, "actual loaded Graphics required");
	for(int player = 0; player < PlayerColor::PLAYER_LIMIT_I; ++player)
	{
		ScalableImageParameters old(original, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		old.setOverlayColor(original, Colors::TRANSPARENCY, true);
		ScalableImageParameters preview(original, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION);
		old.playerColored(PlayerColor(player));
		preview.playerColored(PlayerColor(player));
		samePalette(old, preview);
		for(int i = 224; i < 256; ++i)
		{
			const auto & expected = graphics->playerColorPalette[player][i-224];
			const auto & actual = preview.palette->colors[i];
			require(actual.r == expected.r && actual.g == expected.g && actual.b == expected.b && actual.a == expected.a, "player color changed");
		}
	}
}

int main()
{
	try
	{
		for(bool matching2 : {false, true})
			for(bool matching3 : {false, true})
				runIndexed(matching2, matching3);
		ScalableImageParameters old(nullptr, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
		ScalableImageParameters preview(nullptr, EImageBlitMode::WITH_SHADOW_HIDE_SELECTION);
		SDL_Surface * rgba = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
		require(rgba != nullptr, "RGBA allocation failed");
		for(int y = 0; y < 16; ++y)
			for(int x = 0; x < 16; ++x)
			{
				auto * pixel = static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch + x * 4;
				pixel[0] = uint8_t(x*16); pixel[1] = uint8_t(y*16); pixel[2] = 85; pixel[3] = uint8_t((x+y)*8);
			}
		comparePixels(rgba, old, preview, 1);
		comparePixels(rgba, old, preview, 2);
		for(int flip = 0; flip < 4; ++flip)
		{
			comparePixels(rgba, old, preview, 1, true, flip);
			comparePixels(rgba, old, preview, 2, true, flip);
		}
		SDL_DestroySurface(rgba);
		std::cout << "PASS: four reserved-color profiles, all256, shadow6/7, effect/grayscale, native/2x indexed and RGBA surface pixels. Player resource hook/cache/renderer UNRUN.\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
