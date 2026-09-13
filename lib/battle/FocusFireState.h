/*
 * FocusFireState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HeroCommand.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

/// Round-owned, value-only mark. Inactive targets remain referenced for same-ID revival.
struct DLL_LINKAGE FocusFireState
{
	uint32_t targetUnitId = std::numeric_limits<uint32_t>::max();
	std::vector<uint32_t> recipientUnitIds;
	int32_t issuedRound = 0;
	int32_t rangedDamagePercent = 0;

	bool operator==(const FocusFireState &) const = default;

	void validateShape() const
	{
		const auto maxWireId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
		if(targetUnitId > maxWireId || issuedRound < 1 || recipientUnitIds.empty()
			|| rangedDamagePercent < heroCommands::MIN_EFFECT_PERCENT
			|| rangedDamagePercent > heroCommands::MAX_EFFECT_PERCENT
			|| !std::is_sorted(recipientUnitIds.begin(), recipientUnitIds.end())
			|| std::adjacent_find(recipientUnitIds.begin(), recipientUnitIds.end()) != recipientUnitIds.end()
			|| recipientUnitIds.back() > maxWireId
			|| std::binary_search(recipientUnitIds.begin(), recipientUnitIds.end(), targetUnitId))
			throw std::runtime_error("Invalid New Horizons Focus Fire state shape");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & targetUnitId;
		h & recipientUnitIds;
		h & issuedRound;
		h & rangedDamagePercent;
		if(!h.saving)
			validateShape();
	}
};
