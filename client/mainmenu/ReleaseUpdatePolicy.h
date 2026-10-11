/* New Horizons release update policy. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace releaseUpdates
{
enum class Platform
{
	LinuxX64,
	WindowsX64
};

struct Offer
{
	std::string version;
	std::string releasePage;
};

/// Pure, fail-closed policy. No network, logging, disk, UI or installation.
/// Transport failure/HTTP errors and malformed responses produce no offer.
/// installedVersion is the product config version, never the engine version.
std::optional<Offer> selectOffer(std::string_view installedVersion, Platform platform,
	int httpStatus, std::string_view response);
}
