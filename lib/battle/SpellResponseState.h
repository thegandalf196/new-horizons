/*
 * SpellResponseState.h, part of VCMI engine
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

/// A side-owned response window opened by an accepted enemy hero spell.
/// The response remains available during the trigger round and the next round.
struct DLL_LINKAGE SpellResponseState
{
	/// Round in which the response was armed, or -1 when no response is pending.
	int32_t armedInRound = -1;

	bool hasState() const
	{
		return armedInRound >= 0;
	}

	bool isValid() const
	{
		return armedInRound >= -1;
	}

	bool isReadyAt(int32_t round) const
	{
		return hasState() && round >= armedInRound
			&& static_cast<int64_t>(round) - static_cast<int64_t>(armedInRound) <= 1;
	}

	void validate() const
	{
		if(!isValid())
			throw std::runtime_error("Invalid New Horizons Spell Response round");
	}

	void armAt(int32_t round)
	{
		if(round < 0)
			throw std::runtime_error("Cannot arm New Horizons Spell Response at a negative round");
		armedInRound = round;
	}

	bool consumeAt(int32_t round)
	{
		if(!isReadyAt(round))
			return false;
		armedInRound = -1;
		return true;
	}

	auto operator<=>(const SpellResponseState &) const = default;

	template <typename Handler>
	void serialize(Handler & h)
	{
		if(h.saving)
			validate();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SPELL_RESPONSE))
		{
			h & armedInRound;
			if(!h.saving)
				validate();
		}
		else if(h.saving && hasState())
			throw std::runtime_error("Cannot discard New Horizons Spell Response state in an older format");
		else if(!h.saving)
			armedInRound = -1;
	}
};
