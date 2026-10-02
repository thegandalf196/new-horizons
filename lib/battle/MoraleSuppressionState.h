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

/// Per-side Morale-trigger suppression allowances: Unbreakable once per round,
/// followed by Rally once per battle.
struct DLL_LINKAGE MoraleSuppressionState
{
	bool enabled = false;
	bool used = false;
	bool roundEnabled = false;
	bool roundUsed = false;

	bool available() const
	{
		return enabled && !used;
	}

	bool roundAvailable() const
	{
		return roundEnabled && !roundUsed;
	}

	void nextRound()
	{
		roundUsed = false;
	}

	void validate() const
	{
		if(used && !enabled)
			throw std::runtime_error("Rally Morale suppression used without an enabled perk");
		if(roundUsed && !roundEnabled)
			throw std::runtime_error("Unbreakable Morale suppression used without an enabled perk");
	}

	/// Consumes at most one allowance for a realized negative Morale trigger.
	/// Unbreakable takes precedence over Rally when both are available.
	bool consume(bool negative)
	{
		if(!negative)
			return false;
		if(roundAvailable())
		{
			roundUsed = true;
			return true;
		}
		if(!available())
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
			throw std::runtime_error("Cannot downgrade Morale suppression state");

		if(h.hasFeature(feature))
		{
			h & enabled;
			h & used;
		}
		else if(!h.saving)
		{
			*this = {};
		}

		const auto roundFeature = Handler::Version::NEW_HORIZONS_UNBREAKABLE;
		if(h.hasFeature(roundFeature))
		{
			h & roundEnabled;
			h & roundUsed;
		}
		else if(h.saving && (roundEnabled || roundUsed))
			throw std::runtime_error("Cannot downgrade Unbreakable Morale suppression state");
		else if(!h.saving)
		{
			roundEnabled = false;
			roundUsed = false;
		}

		validate();
	}
};
