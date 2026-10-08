/*
 * ArmorerDefiantState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <stdexcept>

#include "../serializer/ESerializationVersion.h"

namespace newHorizonsArmorer
{
enum class DefiantDenialCause : uint8_t
{
	INNATE_BLOCK,
	NO_QUARTER,
	EXPERT_SHROUD
};
}

/// One hero-side exemption per round. The historical stamp is never reset by activation hooks.
struct DLL_LINKAGE ArmorerDefiantState
{
	int32_t lastConsumedRound = -1;

	bool availableAt(int32_t round) const
	{
		return round >= 0 && lastConsumedRound < round;
	}

	bool consumeAt(int32_t round)
	{
		if(!availableAt(round))
			return false;
		lastConsumedRound = round;
		return true;
	}

	void validate() const
	{
		if(lastConsumedRound < -1)
			throw std::runtime_error("Invalid Defiant consumption round");
	}

	bool operator==(const ArmorerDefiantState &) const = default;

	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(lastConsumedRound >= 0 && !h.hasFeature(Handler::Version::NEW_HORIZONS_ARMORER_DEFIANT))
			throw std::runtime_error("Cannot discard Defiant consumption history");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_ARMORER_DEFIANT))
			h & lastConsumedRound;
		else if(!h.saving)
			*this = {};
		validate();
	}
};
