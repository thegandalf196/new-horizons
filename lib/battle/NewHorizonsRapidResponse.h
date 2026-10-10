/*
 * NewHorizonsRapidResponse.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "BattleSide.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

class CBattleInfoCallback;
namespace battle { class Unit; }

struct DLL_LINKAGE RapidResponseState
{
	static constexpr uint32_t INVALID_UNIT_ID = std::numeric_limits<uint32_t>::max();
	bool enabled = false;
	int32_t lastUsedRound = -1;
	int32_t pendingRound = -1;
	uint32_t pendingUnitId = INVALID_UNIT_ID;

	bool operator==(const RapidResponseState &) const = default;
	bool pending() const { return pendingUnitId != INVALID_UNIT_ID; }
	void clearPending() { pendingRound = -1; pendingUnitId = INVALID_UNIT_ID; }
	void validateShape() const
	{
		if(lastUsedRound < -1 || pendingRound < -1
			|| pending() != (pendingRound >= 0)
			|| (pending() && lastUsedRound >= pendingRound)
			|| (!enabled && *this != RapidResponseState{}))
			throw std::runtime_error("Invalid Rapid Response state");
	}
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validateShape();
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RAPID_RESPONSE) && *this != RapidResponseState{})
			throw std::runtime_error("Cannot discard Rapid Response state");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RAPID_RESPONSE))
		{
			h & enabled;
			h & lastUsedRound;
			h & pendingRound;
			h & pendingUnitId;
		}
		else if(!h.saving)
			*this = {};
		validateShape();
	}
};

namespace newHorizonsRapidResponse
{
inline constexpr std::string_view PERK_KEY = "new-horizons:battlecraft.rapidResponse";
DLL_LINKAGE bool eligible(const CBattleInfoCallback & battle, BattleSide side, const battle::Unit * unit);
DLL_LINKAGE const battle::Unit * latestWaiter(const CBattleInfoCallback & battle, BattleSide side);
DLL_LINKAGE const battle::Unit * pendingWaiter(const CBattleInfoCallback & battle, BattleSide side);
/// Pure transition plans, also used by detached simulations. No RNG or new activation.
DLL_LINKAGE RapidResponseState capture(const CBattleInfoCallback & battle, BattleSide side);
DLL_LINKAGE RapidResponseState resolve(const CBattleInfoCallback & battle, BattleSide side, bool consume);
}
