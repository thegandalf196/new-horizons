/*
 * SDLImageScaler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SDLImageScaler.h"

#include "SDL_Extensions.h"

#include "GameEngine.h"
#include "xBRZ/xbrz.h"

#include <tbb/parallel_for.h>
#include <SDL3/SDL_surface.h>


namespace
{
bool renderLoadTimingsEnabled()
{
	static const bool requested = []
	{
		const char * value = std::getenv("NH_RENDER_LOAD_TIMINGS");
		return value && std::string_view(value) == "1";
	}();
	return requested && logGlobal->isDebugEnabled();
}

// Per-TU cap: at most 1024 event records, plus one overflow notice.
// The counter retains the total attempted events, including dropped records.
bool reserveRenderLoadTiming()
{
	if(!renderLoadTimingsEnabled())
		return false;
	static std::atomic<uint64_t> attempts{0};
	const auto index = attempts.fetch_add(1, std::memory_order_relaxed);
	if(index == 1024)
		logGlobal->debug("NH_RENDER_LOAD_TIMING cap=1024 reached; subsequent events counted but not timed");
	return index < 1024;
}

using RenderLoadClock = std::chrono::steady_clock;
struct RenderLoadTiming
{
	bool active = reserveRenderLoadTiming();
	RenderLoadClock::time_point start = active ? RenderLoadClock::now() : RenderLoadClock::time_point{};
	long long elapsed() const
	{
		return active ? std::chrono::duration_cast<std::chrono::microseconds>(RenderLoadClock::now() - start).count() : 0;
	}
};

}

SDLImageOptimizer::SDLImageOptimizer(SDL_Surface * surf, const Rect & virtualDimensions)
	: surf(surf)
	, virtualDimensions(virtualDimensions)
{
}

void SDLImageOptimizer::optimizeSurface(SDL_Surface * formatSourceSurface)
{
	if (!surf)
		return;

	int left = surf->w;
	int top = surf->h;
	int right = 0;
	int bottom = 0;

	// locate fully-transparent area around image
	// H3 hadles this on format level, but mods or images scaled in runtime do not
	if (CSDL_Ext::getPalette(surf))
	{
		for (int y = 0; y < surf->h; ++y)
		{
			const uint8_t * row = static_cast<uint8_t *>(surf->pixels) + y * surf->pitch;
			for (int x = 0; x < surf->w; ++x)
			{
				if (row[x] != 0)
				{
					// opaque or can be opaque (e.g. disabled shadow)
					top = std::min(top, y);
					left = std::min(left, x);
					right = std::max(right, x);
					bottom = std::max(bottom, y);
				}
			}
		}
	}
	else
	{
		// hoisted out of the loop, resolving the format for every pixel is not free
		const SDL_PixelFormatDetails * format = CSDL_Ext::getFormat(surf);

		for (int y = 0; y < surf->h; ++y)
		{
			for (int x = 0; x < surf->w; ++x)
			{
				uint8_t alpha;
				SDL_GetRGBA(CSDL_Ext::getPixel(surf, x, y), format, nullptr, nullptr, nullptr, nullptr, &alpha);

				if (alpha != SDL_ALPHA_TRANSPARENT)
				{
					// opaque
					top = std::min(top, y);
					left = std::min(left, x);
					right = std::max(right, x);
					bottom = std::max(bottom, y);
				}
			}
		}
	}

	// empty image
	if (left == surf->w)
		return;

	if (left != 0 || top != 0 || right != surf->w - 1 || bottom != surf->h - 1)
	{
		// non-zero border found
		Rect newDimensions(left, top, right - left + 1, bottom - top + 1);
		SDL_Rect rectSDL = CSDL_Ext::toSDL(newDimensions);
		auto newSurface = CSDL_Ext::newSurface(newDimensions.dimensions(), formatSourceSurface);
		SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(surf, &rectSDL, newSurface, nullptr);

		if (SDL_SurfaceHasColorKey(surf))
		{
			uint32_t colorKey;
			SDL_GetSurfaceColorKey(surf, &colorKey);
			SDL_SetSurfaceColorKey(newSurface, true, colorKey);
		}
		output = newSurface;

		virtualDimensions.x += left;
		virtualDimensions.y += top;
	}
	else
	{
		output = surf;
		output->refcount += 1;
	}
}

SDL_Surface * SDLImageOptimizer::acquireResultSurface()
{
	SDL_Surface * result = output;
	output = nullptr;
	return result;
}

const Rect & SDLImageOptimizer::getResultDimensions() const
{
	return virtualDimensions;
}

void SDLImageScaler::scaleSurface(Point targetDimensions, EScalingAlgorithm algorithm)
{
	if (!intermediate)
		return; // may happen on scaling of empty images

	if(!targetDimensions.x || !targetDimensions.y)
		throw std::runtime_error("invalid scaling dimensions!");

	Point inputSurfaceSize(intermediate->w, intermediate->h);
	Point outputSurfaceSize = targetDimensions * inputSurfaceSize / virtualDimensionsInput.dimensions();
	Point outputMargins = targetDimensions * virtualDimensionsInput.topLeft() / virtualDimensionsInput.dimensions();

	// TODO: use xBRZ if possible? E.g. when scaling to 150% do 100% -> 200% via xBRZ and then linear downscale 200% -> 150%?
	// Need to investigate which is optimal	for performance and for visuals
	ret = CSDL_Ext::newSurface(Point(outputSurfaceSize.x, outputSurfaceSize.y), intermediate);

	virtualDimensionsOutput = Rect(outputMargins, targetDimensions); // TODO: account for input virtual size

	const uint32_t * srcPixels = static_cast<const uint32_t*>(intermediate->pixels);
	uint32_t * dstPixels = static_cast<uint32_t*>(ret->pixels);

	RenderLoadTiming resizeTiming;
	if (algorithm == EScalingAlgorithm::NEAREST)
		xbrz::nearestNeighborScale(srcPixels, intermediate->w, intermediate->h, dstPixels, ret->w, ret->h);
	else
		xbrz::bilinearScale(srcPixels, intermediate->w, intermediate->h, dstPixels, ret->w, ret->h);
	if(resizeTiming.active)
		logGlobal->debug("NH_RENDER_LOAD_TIMING stage=resizeKernel scaler=%p input=%p output=%p algorithm=%d width=%d height=%d us=%d",
			static_cast<void *>(this), static_cast<void *>(intermediate), static_cast<void *>(ret),
			static_cast<int>(algorithm), ret->w, ret->h, resizeTiming.elapsed());
}

void SDLImageScaler::scaleSurfaceIntegerFactor(int factor, EScalingAlgorithm algorithm)
{
	if (!intermediate)
		return; // may happen on scaling of empty images

	if(factor == 0)
		throw std::runtime_error("invalid scaling factor!");

	int newWidth = intermediate->w * factor;
	int newHight = intermediate->h * factor;

	virtualDimensionsOutput = virtualDimensionsInput * factor;

	ret = CSDL_Ext::newSurface(Point(newWidth, newHight), intermediate);

	assert(intermediate->pitch == intermediate->w * 4);
	assert(ret->pitch == ret->w * 4);

	const uint32_t * srcPixels = static_cast<const uint32_t*>(intermediate->pixels);
	uint32_t * dstPixels = static_cast<uint32_t*>(ret->pixels);

	RenderLoadTiming kernelTiming;
	switch (algorithm)
	{
		case EScalingAlgorithm::NEAREST:
			xbrz::nearestNeighborScale(srcPixels, intermediate->w, intermediate->h, dstPixels, ret->w, ret->h);
			break;
		case EScalingAlgorithm::BILINEAR:
			xbrz::bilinearScale(srcPixels, intermediate->w, intermediate->h, dstPixels, ret->w, ret->h);
			break;
		case EScalingAlgorithm::XBRZ_ALPHA:
		case EScalingAlgorithm::XBRZ_OPAQUE:
		{
			auto format = algorithm == EScalingAlgorithm::XBRZ_OPAQUE ? xbrz::ColorFormat::ARGB_CLAMPED : xbrz::ColorFormat::ARGB;

			if(intermediate->h < 32)
			{
				// for tiny images tbb incurs too high overhead
				xbrz::scale(factor, srcPixels, dstPixels, intermediate->w, intermediate->h, format, {});
			}
			else
			{
				// xbrz recommends granulation of 16, but according to tests, for smaller images granulation of 4 is actually the best option
				const int granulation = intermediate->h > 400 ? 16 : 4;
				tbb::parallel_for(tbb::blocked_range<size_t>(0, intermediate->h, granulation), [this, factor, srcPixels, dstPixels, format](const tbb::blocked_range<size_t> & r)
								  {
									  xbrz::scale(factor, srcPixels, dstPixels, intermediate->w, intermediate->h, format, {}, r.begin(), r.end());
								  });
			}

			break;
		}
		default:
			throw std::runtime_error("invalid scaling algorithm!");
	}
	if(kernelTiming.active)
		logGlobal->debug("NH_RENDER_LOAD_TIMING stage=scaleKernel scaler=%p input=%p output=%p factor=%d algorithm=%d width=%d height=%d us=%d",
			static_cast<void *>(this), static_cast<void *>(intermediate), static_cast<void *>(ret), factor,
			static_cast<int>(algorithm), newWidth, newHight, kernelTiming.elapsed());
}

SDLImageScaler::SDLImageScaler(SDL_Surface * surf)
	:SDLImageScaler(surf, Rect(0,0,surf->w, surf->h), false)
{
}

SDLImageScaler::SDLImageScaler(SDL_Surface * surf, const Rect & virtualDimensions, bool optimizeImage)
{
	if (optimizeImage)
	{
		SDLImageOptimizer optimizer(surf, virtualDimensions);
		RenderLoadTiming cropTiming;
		optimizer.optimizeSurface(nullptr);
		intermediate = optimizer.acquireResultSurface();
		virtualDimensionsInput = optimizer.getResultDimensions();
		if(cropTiming.active)
			logGlobal->debug("NH_RENDER_LOAD_TIMING stage=scalerCrop scaler=%p input=%p output=%p width=%d height=%d us=%d",
				static_cast<void *>(this), static_cast<void *>(surf), static_cast<void *>(intermediate),
				intermediate ? intermediate->w : 0, intermediate ? intermediate->h : 0, cropTiming.elapsed());
	}
	else
	{
		intermediate = surf;
		intermediate->refcount += 1;
		virtualDimensionsInput = virtualDimensions;
	}

	if (intermediate == surf)
	{
		SDL_DestroySurface(intermediate);
		RenderLoadTiming conversionTiming;
		intermediate = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
		if(conversionTiming.active)
			logGlobal->debug("NH_RENDER_LOAD_TIMING stage=scalerArgb scaler=%p input=%p output=%p us=%d",
				static_cast<void *>(this), static_cast<void *>(surf), static_cast<void *>(intermediate), conversionTiming.elapsed());
	}

	if (intermediate == surf)
		throw std::runtime_error("Scaler uses same surface as input!");
}

SDLImageScaler::~SDLImageScaler()
{
	ENGINE->dispatchMainThread([surface = intermediate]()
	{
		// SDL_DestroySurface must be executed in main thread to avoid thread races to its internal state
		SDL_DestroySurface(surface);
	});
}

SDL_Surface * SDLImageScaler::acquireResultSurface()
{
	SDL_Surface * result = ret;
	ret = nullptr;
	return result;
}

const Rect & SDLImageScaler::getResultDimensions() const
{
	return virtualDimensionsOutput;
}
