/*
 * NewHorizonsLegendaryReputation.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

class CGameState;
class CGHeroInstance;
struct RebalanceStacks;
struct BulkRebalanceStacks;
struct SwapStacks;

namespace newHorizonsDiplomacy
{
/// Causal witness of one first positive admission from an accepted free offer.
struct DLL_LINKAGE LegendaryAdmission
{
	ObjectInstanceID hero;
	ObjectInstanceID source;
	SlotID sourceSlot;
	CreatureID creature;
	int32_t month = -1;
	int32_t previousMonth = -1;
	int64_t originalQuantity = 0;
	int64_t sourceQuantity = 0;
	int64_t normalGoldCost = 0;
	uint64_t originalArmyValue = 0;
	bool recruitmentPactApplied = false;

	bool operator==(const LegendaryAdmission &) const = default;

	void validate() const
	{
		if(!hero.hasValue() || !source.hasValue() || hero == source || !sourceSlot.validSlot()
			|| !creature.hasValue() || month < 1 || previousMonth < -1 || previousMonth == 0
			|| previousMonth >= month || originalQuantity <= 0 || sourceQuantity < originalQuantity
			|| normalGoldCost <= 0 || normalGoldCost > std::numeric_limits<int32_t>::max()
			|| sourceQuantity > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Invalid Legendary Reputation admission receipt");
	}

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & hero; h & source; h & sourceSlot; h & creature;
		h & month; h & previousMonth; h & originalQuantity; h & sourceQuantity; h & normalGoldCost;
		// Signed varints cannot encode uint64_t directly. Preserve the full
		// unsigned range through two bounded signed halves, as mastery receipts do.
		constexpr int64_t maximumHalf = std::numeric_limits<uint32_t>::max();
		constexpr int halfBits = std::numeric_limits<uint32_t>::digits;
		int64_t high = static_cast<int64_t>(originalArmyValue >> halfBits);
		int64_t low = static_cast<int64_t>(originalArmyValue & static_cast<uint64_t>(maximumHalf));
		h & high;
		h & low;
		if(!h.saving)
		{
			if(high < 0 || high > maximumHalf || low < 0 || low > maximumHalf)
				throw std::runtime_error("Invalid Legendary Reputation army value wire halves");
			originalArmyValue = (static_cast<uint64_t>(high) << halfBits) | static_cast<uint64_t>(low);
		}
		h & recruitmentPactApplied;
		if(!h.saving) validate();
	}
};

/// Read-only semantic preflight. Month publication is separate from troop application.
struct DLL_LINKAGE PreparedGarrisonAdmission
{
	CGHeroInstance * hero = nullptr;
	int32_t month = -1;
	void commit() const;
};
DLL_LINKAGE PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const RebalanceStacks & pack);
DLL_LINKAGE PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const BulkRebalanceStacks & pack);
DLL_LINKAGE PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const SwapStacks & pack);
}
