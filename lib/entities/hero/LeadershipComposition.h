/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <stdexcept>

namespace newHorizonsHeroes
{
/// Original army composition, captured before battle summons or casualties.
/// A false captured bit is historical absence, never an invitation to inspect
/// a live adventure army from a hypothetical battle.
struct DLL_LINKAGE LeadershipComposition
{
	bool captured = false;
	bool regimentalOrganization = false;
	bool combinedArms = false;

	bool operator==(const LeadershipComposition &) const = default;
	void validate() const
	{
		if(!captured && (regimentalOrganization || combinedArms))
			throw std::runtime_error("Leadership composition flags require a captured army");
	}
	void validateSerialization(bool supported) const
	{
		validate();
		if(!supported && captured)
			throw std::runtime_error("Cannot discard battle-start Leadership composition");
	}

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY))
		{
			if(h.saving && captured)
				throw std::runtime_error("Cannot discard battle-start Leadership composition");
			if(!h.saving) *this = {};
			return;
		}
		h & captured;
		h & regimentalOrganization;
		h & combinedArms;
		if(!h.saving) validate();
	}
};
}
