/* New Horizons release update controller. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "ReleaseUpdatePolicy.h"

#include <memory>
#include <mutex>
#include <thread>

namespace releaseUpdates
{
/// GUI-owned worker. Destruction requests cancellation and joins before state dies.
class ReleaseUpdateController
{
	struct State
	{
		std::mutex mutex;
		std::optional<Offer> offer;
	};
	std::shared_ptr<State> state;
	std::jthread worker;

public:
	ReleaseUpdateController(std::string installedVersion, bool enabled);
	std::optional<Offer> takeOffer();
};
}
