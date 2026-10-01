/*
 * MoraleSuppressionState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <stdexcept>

#include "../serializer/ESerializationVersion.h"

/// Per-side, battle-long allowance to cancel one negative Morale trigger.
struct DLL_LINKAGE MoraleSuppressionState
{
	bool enabled = false;
	bool used = false;

	bool available() const
	{
		return enabled && !used;
	}

	/// Consumes the allowance only when a negative Morale trigger is realized.
	bool consume(bool negative)
	{
		if(!negative || !available())
			return false;
		used = true;
		return true;
	}

	bool operator==(const MoraleSuppressionState &) const = default;

	template <typename Handler>
	void serialize(Handler & h)
	{
		const auto feature = Handler::Version::NEW_HORIZONS_RALLY;
		if(h.saving && !h.hasFeature(feature) && *this != MoraleSuppressionState{})
			throw std::runtime_error("Cannot downgrade Rally Morale suppression state");

		if(h.hasFeature(feature))
		{
			h & enabled;
			h & used;
		}
		else if(!h.saving)
		{
			*this = {};
		}

		if(!h.saving && used && !enabled)
			throw std::runtime_error("Rally Morale suppression used without an enabled perk");
	}
};
