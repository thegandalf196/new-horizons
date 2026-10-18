/*
 * CureIntegrationBootstrapState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace cureBootstrap
{
inline constexpr std::string_view MAP = "Maps/ordinary48-normal-garrison-ice200-three-human-neutral-owner.vmap";
inline constexpr std::string_view DIGEST = "44b24ce7605c482609df466550e03e57528b59b5892be1cfc58fb6e71ecf4313";

inline void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

struct Header
{
	std::array<bool, 8> human{};
	std::array<bool, 8> computer{};
	int width = 0;
	int height = 0;
	bool normal = false;
	bool ordinary = false;
};

struct Loaded
{
	std::array<bool, 8> human{};
	std::array<bool, 8> computer{};
	bool exactMap = false;
	bool newGame = false;
	bool normal = false;
	bool redTyris = false;
	bool threeRedStacks = false;
	bool neutralIce200 = false;
};

inline void validateResource(bool managedProfile, std::size_t bytes, std::string_view digest)
{
	require(managedProfile && bytes == 1581 && digest == DIGEST,
		"Cure bootstrap requires exact managed-profile fixture44b24/1581B");
}

inline void validateHeader(const Header & header)
{
	const std::array<bool, 8> human{true, false, false, false, false, false, false, false};
	const std::array<bool, 8> computer{true, true, false, false, false, false, false, false};
	require(header.human == human && header.computer == computer && header.normal && header.ordinary
		&& header.width == 36 && header.height == 36, "Unexpected Cure bootstrap map header");
}

inline void validateLoaded(const Loaded & loaded)
{
	const std::array<bool, 8> human{true, false, false, false, false, false, false, false};
	const std::array<bool, 8> computer{false, true, false, false, false, false, false, false};
	require(loaded.exactMap && loaded.newGame && loaded.normal
		&& loaded.human == human && loaded.computer == computer,
		"Cure bootstrap requires actual Normal/red-human/blue-AI world");
	require(loaded.redTyris && loaded.threeRedStacks && loaded.neutralIce200,
		"Cure bootstrap requires actual Tyris/three friendly stacks/neutral Ice200");
}

/// Consumed before any validation/connection; never rearmed by failure or restart.
class State
{
	bool pending = false;
	bool requested = false;
	bool validated = false;

public:
	explicit State(const char * optIn)
	{
		if(optIn)
		{
			require(std::string_view(optIn) == "ordinary44b24", "Invalid NH_CURE_BOOTSTRAP opt-in");
			pending = true;
		}
	}

	bool consume()
	{
		if(!pending)
			return false;
		pending = false;
		requested = true;
		return true;
	}

	bool awaitingLoaded() const { return requested && !validated; }
	void acceptLoaded() { validated = true; }
};
}
