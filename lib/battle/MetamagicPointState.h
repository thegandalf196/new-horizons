/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../Global.h"
#include <compare>
#include <cstdint>
#include <stdexcept>

/// Side-owned, non-spendable combat growth; distinct from historical Arcane Marks.
struct DLL_LINKAGE MetamagicPointState
{
	uint8_t points = 0;
	bool singularityReached = false;
	bool echoUsedThisRound = false;

	auto operator<=>(const MetamagicPointState &) const = default;

	void validate() const
	{
		if(points > 12 || (singularityReached && points < 10))
			throw std::runtime_error("Invalid Metamagic point state");
	}

	void validateSerialization(bool supported) const
	{
		validate();
		if(!supported && *this != MetamagicPointState{})
			throw std::runtime_error("Cannot discard Metamagic point state in an older format");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS));
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS))
		{
			h & points;
			h & singularityReached;
			h & echoUsedThisRound;
			validate();
		}
		else if(!h.saving)
			*this = {};
	}
};
