/*
 * NewHorizonsSeizeInitiative.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "BattleUnitTurnReason.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

class CBattleInfoCallback;

struct DLL_LINKAGE SeizeInitiativeState
{
	static constexpr uint32_t NO_UNIT = std::numeric_limits<uint32_t>::max();
	struct DLL_LINKAGE Receipt
	{
		bool enabled = false;
		bool triggered = false;
		uint32_t recipient = NO_UNIT;
		uint32_t anchor = NO_UNIT;
		bool awaitingAnchor = false;
		bool operator==(const Receipt &) const = default;
		template <typename Handler> void serialize(Handler & h)
		{
			h & enabled; h & triggered; h & recipient; h & anchor; h & awaitingAnchor;
		}
	};
	std::array<Receipt, 2> sides;
	int32_t round = -1;
	uint32_t active = NO_UNIT;
	bool activeNormal = false;
	std::vector<uint32_t> normalCompleted;
	bool operator==(const SeizeInitiativeState &) const = default;
	bool enabled() const { return sides[0].enabled || sides[1].enabled; }
	void validateShape() const;
	void nextRound(int32_t next);
	void begin(uint32_t id, bool normal);
	void complete(uint32_t id);
	bool completed(uint32_t id) const
	{
		return std::binary_search(normalCompleted.begin(), normalCompleted.end(), id);
	}
	template <typename Handler> void validateSerialization(Handler & h) const
	{
		validateShape();
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_SEIZE_INITIATIVE)
			&& *this != SeizeInitiativeState{})
			throw std::runtime_error("Cannot discard Seize Initiative/normal activation state");
	}
	template <typename Handler> void serialize(Handler & h)
	{
		validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SEIZE_INITIATIVE))
		{
			h & sides; h & round; h & active; h & activeNormal; h & normalCompleted;
			validateShape();
		}
		else if(!h.saving)
			*this = {};
	}
};

namespace newHorizonsSeizeInitiative
{
inline constexpr std::string_view SKILL_KEY = "new-horizons:command";
inline constexpr std::string_view PERK_KEY = "new-horizons:command.seizeInitiative";
DLL_LINKAGE bool eligible(const CBattleInfoCallback &, BattleSide, const battle::Unit *);
DLL_LINKAGE SeizeInitiativeState capturePaidOrder(const CBattleInfoCallback &, BattleSide);
/// Permutes only friendly positions; every enemy remains in its original slot.
DLL_LINKAGE void reorder(const CBattleInfoCallback &, battle::Units &, bool protectActive);
DLL_LINKAGE void validateReferences(const CBattleInfoCallback &, const SeizeInitiativeState &);
}
