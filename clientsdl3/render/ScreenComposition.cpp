/*
 * Screen composition, part of VCMI / New Horizons.
 * Based on vcmi/vcmi develop 948333932fdf91abda780906bf2d18eec9aa0fc1.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "ScreenHandler.h"
#include "GpuResources.h"
#include "Canvas.h"
#include "SDLImage.h"
#include "SDL_Extensions.h"
#include "../../client/PerfTrace.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "CServerHandler.h"
#include "GameChatHandler.h"
#include "gui/CursorHandler.h"
#include "render/IRenderHandler.h"
#include "lib/VCMIDirs.h"
#include "lib/texts/MetaString.h"
#include <vstd/DateUtils.h>
#include <SDL3/SDL.h>
#include <chrono>

namespace
{
class RenderClipGuard
{
	SDL_Renderer * renderer;
	SDL_Rect previous{};
	bool enabled;
public:
	explicit RenderClipGuard(SDL_Renderer * renderer) : renderer(renderer),
		enabled(SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &previous)) {}
	~RenderClipGuard() { SDL_SetRenderClipRect(renderer, enabled ? &previous : nullptr); }
};
}

bool ScreenHandler::presentFromCanvas(GpuRenderLayer layer, const Canvas & source,
	const std::vector<PresentedRegion> & regions, const Rect & clip, const void * owner)
{
	if(!isGpuRenderingEnabled() || !directMapPixels || !owner || regions.empty() || !source.isRenderTarget()
		|| source.getRenderTargetGeneration() != GpuResources::get().generation())
		return false;
	if(getRenderResolution() != buffersRenderResolution)
		return false; // SDL may apply a window resize before its event is delivered.
	std::scoped_lock registrationLock(presentedMutex);
	const size_t index = static_cast<size_t>(layer);
	auto & state = presentedCanvases.at(index);
	state.owner = owner;
	state.texture = source.getRenderTargetTexture();
	state.generation = source.getRenderTargetGeneration();
	state.clip = clip;
	state.regions = regions;
	// Do not enqueue a deferred release that would erase this new registration.
	// Old layer pixels are cleared when a later overlay asks for the layer again.
	layerReleasedMask &= ~(1u << index);
	layerActive[index] = false;
	return true;
}

void ScreenHandler::clearPresentedCanvas(GpuRenderLayer layer, const void * owner)
{
	std::scoped_lock registrationLock(presentedMutex);
	presentedCanvases.at(static_cast<size_t>(layer)).clear(owner);
}

void ScreenHandler::clearAllPresentedCanvases()
{
	std::scoped_lock registrationLock(presentedMutex);
	for(auto & state : presentedCanvases)
		state.clear();
}

void ScreenHandler::composeFrame() const
{
	std::scoped_lock registrationLock(presentedMutex);
	SDL_Renderer * renderer = GpuResources::get().renderer();
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	for(size_t i = 0; i < layerTextures.size(); ++i)
	{
		const auto & state = presentedCanvases[i];
		if(isGpuRenderingEnabled() && state.current(GpuResources::get().generation()))
		{
			RenderClipGuard restoreClip(renderer);
			const SDL_Rect clip{state.clip.x, state.clip.y, state.clip.w, state.clip.h};
			SDL_SetRenderClipRect(renderer, &clip);
			SDL_SetTextureBlendMode(state.texture, SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(state.texture, SDL_SCALEMODE_NEAREST);
			for(const auto & region : state.regions)
			{
				const SDL_FRect from = CSDL_Ext::toSDLFloat(region.source);
				const SDL_FRect to = CSDL_Ext::toSDLFloat(region.target);
				SDL_RenderTexture(renderer, state.texture, &from, &to);
			}
		}
		if(layerTextures[i] && layerActive[i])
			SDL_RenderTexture(renderer, layerTextures[i], nullptr, nullptr);
	}
	SDL_RenderTexture(renderer, isGpuRenderingEnabled() ? screenTarget : screenTexture, nullptr, nullptr);
}

static bool presentWithPerformanceTrace(SDL_Renderer * renderer)
{
	if(!PerfTrace::enabled())
		return SDL_RenderPresent(renderer);
	const auto started = std::chrono::steady_clock::now();
	const bool succeeded = SDL_RenderPresent(renderer);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	PerfTrace::emit("sdl_present_duration_ns", succeeded,
		std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
	return succeeded;
}

void ScreenHandler::presentScreenTexture()
{
	// the memory cache gives assets up on whichever thread loaded one; freeing what they hold has
	// to wait for this thread, where nothing is drawing from them
	ENGINE->renderHandler().reclaimEvictedAssets();

	SDL_Renderer * renderer = GpuResources::get().renderer();

	// a layer may still be bound from rendering into it
	SDL_SetRenderTarget(renderer, nullptr);

	{
		GpuResources::get().processPendingTextureDestruction();
	}

	composeFrame();
	ENGINE->cursor().render();
	if(presentWithPerformanceTrace(renderer))
		PerfTrace::presented();
	else
		PerfTrace::emit("present_failed");
}

void ScreenHandler::screenShot() const
{
	const boost::filesystem::path outPath = VCMIDirs::get().userExtractedPath() / "screenshots";
	boost::filesystem::create_directories(outPath);
	const boost::filesystem::path filePath = outPath / ("screenshot-" + vstd::getDateTimeISO8601Basic(std::time(nullptr)) + ".png");
	SDL_Surface * pixels = screen;
	if(isGpuRenderingEnabled())
	{
		auto * renderer = GpuResources::get().renderer();
		Canvas frame = createOffscreenCanvas(getLogicalResolution());
		if(!frame.isRenderTarget())
		{
			logGlobal->error("Unable to create screenshot render target");
			return;
		}
		auto * previous = SDL_GetRenderTarget(renderer);
		if(!SDL_SetRenderTarget(renderer, frame.getRenderTargetTexture()))
		{
			logGlobal->error("Unable to bind screenshot render target: %s", SDL_GetError());
			return;
		}
		composeFrame();
		pixels = SDL_RenderReadPixels(renderer, nullptr);
		SDL_SetRenderTarget(renderer, previous);
		if(!pixels)
		{
			logGlobal->error("Unable to read screenshot: %s", SDL_GetError());
			return;
		}
	}
	auto img = std::make_shared<SDLImageShared>(pixels);
	if(pixels != screen)
		SDL_DestroySurface(pixels);
	img->exportBitmap(filePath, nullptr);
	MetaString txt;
	txt.appendTextID("vcmi.client.screenShot");
	txt.replaceRawString(filePath.string());
	if(GAME && GAME->interface())
		GAME->server().getGameChat().sendMessageGameplay(txt.toString(&GAME->translator()));
}

Canvas ScreenHandler::getLayerCanvas(GpuRenderLayer layer)
{
	std::scoped_lock registrationLock(presentedMutex);
	presentedCanvases.at(static_cast<size_t>(layer)).clear();
	const size_t index = static_cast<size_t>(layer);
	if(!layerActive.at(index))
		clearLayer(index);

	layerActive.at(index) = true;
	layerReleasedMask &= ~(1u << index);

	return Canvas::createFromRenderTarget(layerTextures.at(index), getLogicalResolution(), CanvasScalingPolicy::AUTO);
}

void ScreenHandler::releaseLayer(GpuRenderLayer layer)
{
	layerReleasedMask |= 1u << static_cast<size_t>(layer);
}

void ScreenHandler::clearReleasedLayers()
{
	std::scoped_lock registrationLock(presentedMutex);
	// Windows are deactivated when closed and when merely covered, so this runs before the
	// redraw and getLayerCanvas() takes the request back if the owner draws again.
	const uint32_t released = layerReleasedMask.exchange(0);

	if(released == 0)
		return;

	for(size_t i = 0; i < layerTextures.size(); ++i)
	{
		if((released & (1u << i)) == 0)
			continue;
		presentedCanvases[i].clear();
		if(!layerTextures[i])
			continue;
		clearLayer(i);
		layerActive[i] = false;
	}

	SDL_SetRenderTarget(GpuResources::get().renderer(), nullptr);
}

