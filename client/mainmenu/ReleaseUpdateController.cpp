/* New Horizons release update controller. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ReleaseUpdateController.h"
#include "ReleaseUpdateTransport.h"

#include <atomic>

namespace releaseUpdates
{
ReleaseUpdateController::ReleaseUpdateController(std::string installedVersion, bool enabled)
	: state(std::make_shared<State>())
{
	// Menu reconstruction must not issue another request in this process/session.
	static std::atomic_bool started = false;
	if(!enabled || started.exchange(true))
		return;
#if defined(_WIN32)
	constexpr Platform platform = Platform::WindowsX64;
#elif defined(__linux__)
	constexpr Platform platform = Platform::LinuxX64;
#else
	return;
#endif
#if defined(_WIN32) || defined(__linux__)
	try
	{
		worker = std::jthread([sharedState = state, installedVersion = std::move(installedVersion), platform](std::stop_token stop)
		{
			try
			{
				const auto response = fetchLatest(stop);
				if(stop.stop_requested())
					return;
				auto offer = selectOffer(installedVersion, platform, response.status, response.body);
				std::scoped_lock lock(sharedState->mutex);
				sharedState->offer = std::move(offer);
			}
			catch(...)
			{
				// Offline/error/404 is not a startup error and must never interrupt play.
			}
		});
	}
	catch(...)
	{
		// Resource/thread creation failure must not turn an optional check into
		// a startup failure. Leave the session request consumed rather than retry.
	}
#endif
}

std::optional<Offer> ReleaseUpdateController::takeOffer()
{
	std::scoped_lock lock(state->mutex);
	auto offer = std::move(state->offer);
	state->offer.reset();
	return offer;
}
}
