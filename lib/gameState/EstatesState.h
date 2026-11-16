/*
 * Part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../constants/EntityIdentifiers.h"

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

namespace newHorizonsEstates
{
/// One kingdom ledger. Quotes never modify these cumulative base prices.
using ConscriptionLedger = std::array<int64_t, 7>;

struct DLL_LINKAGE KingdomState
{
	ConscriptionLedger recruitmentBaseCost{};
	/// Town -> hero; this is the only authoritative assignment index.
	std::map<ObjectInstanceID, ObjectInstanceID> stewards;

	bool operator==(const KingdomState &) const = default;
	bool empty() const;
	void validate() const;
	void validateSerialization(bool supported) const;

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
		{
			validate();
			h & recruitmentBaseCost;
			h & stewards;
		}
		else
		{
			KingdomState candidate;
			h & candidate.recruitmentBaseCost;
			h & candidate.stewards;
			candidate.validate();
			*this = std::move(candidate);
		}
	}
};
}
