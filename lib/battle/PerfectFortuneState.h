/*
 * PerfectFortuneState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../serializer/ESerializationVersion.h"
#include <stdexcept>

/// Captured Luck perk selection and its independent once-per-combat token.
struct DLL_LINKAGE PerfectFortuneState
{
	bool enabled = false;
	bool used = false;
	bool available() const
	{
		return enabled && !used;
	}
	void validate() const
	{
		if(used && !enabled)
			throw std::runtime_error("Perfect Fortune used without an enabled perk");
	}
	bool operator==(const PerfectFortuneState &) const = default;
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_PERFECT_FORTUNE) && *this != PerfectFortuneState{})
			throw std::runtime_error("Cannot discard Perfect Fortune battle state");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PERFECT_FORTUNE))
		{
			h & enabled;
			h & used;
			if(!h.saving)
				validate();
		}
		else if(!h.saving)
			*this = {};
	}
};
