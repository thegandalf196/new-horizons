/*
 * AdverseCombatRerollState.h, part of VCMI engine
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

/// Per-side, battle-long allowance for rerolling one adverse stochastic result.
struct DLL_LINKAGE AdverseCombatRerollState
{
	bool enabled = false;
	bool used = false;

	bool available() const
	{
		return enabled && !used;
	}

	/// Consumes the allowance only for a stochastic adverse result.
	/// Deterministic effects and favorable results leave it available.
	bool consume(bool stochastic, bool adverse)
	{
		if(!stochastic || !adverse || !available())
			return false;
		used = true;
		return true;
	}

	bool operator==(const AdverseCombatRerollState &) const = default;

	template <typename Handler>
	void serialize(Handler & h)
	{
		const auto feature = Handler::Version::NEW_HORIZONS_ADVERSE_COMBAT_REROLL;
		if(h.saving && !h.hasFeature(feature) && *this != AdverseCombatRerollState{})
			throw std::runtime_error("Cannot downgrade adverse combat reroll state");

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
			throw std::runtime_error("Adverse combat reroll expenditure without an enabled perk");
	}
};
