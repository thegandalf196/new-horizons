/* New Horizons public release transport. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <stop_token>
#include <string>

namespace releaseUpdates
{
struct Response
{
	int status = 0;
	std::string body;
};

/// Call only from an owned background task. No UI, logs, credentials or writes.
/// Network/cancellation/size-limit failures return {0, {}} silently.
Response fetchLatest(std::stop_token stop);
}
