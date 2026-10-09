/*
 * NewHorizonsMagnateIncome.h, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <cstdint>
#include <stdexcept>

namespace newHorizonsEconomy
{
/// Fixed town award, independent of the granting heroes' later lifetime.
struct DLL_LINKAGE MagnateIncome
{
	static constexpr int32_t GOLD_PER_HOLDER = 500;
	static constexpr int32_t DURATION_DAYS = 7;
	int32_t dailyGold = 0;
	int32_t startDay = -1;
	PlayerColor awardOwner = PlayerColor::NEUTRAL;

	bool operator==(const MagnateIncome &) const = default;
	bool empty() const { return dailyGold == 0; }
	void validate() const
	{
		if((empty() && (startDay != -1 || awardOwner != PlayerColor::NEUTRAL))
			|| (!empty() && (dailyGold < GOLD_PER_HOLDER || dailyGold % GOLD_PER_HOLDER != 0
				|| startDay < 1 || !awardOwner.isValidPlayer())))
			throw std::runtime_error("Invalid New Horizons Magnate town income");
	}
	void validateSerialization(bool supported) const
	{
		validate();
		if(!supported && !empty())
			throw std::runtime_error("Cannot discard New Horizons Magnate town income");
	}
	int32_t goldOnDay(int32_t day, PlayerColor owner) const
	{
		validate();
		return !empty() && owner == awardOwner && day >= startDay
			&& static_cast<int64_t>(day) < static_cast<int64_t>(startDay) + DURATION_DAYS ? dailyGold : 0;
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGNATE));
		h & dailyGold;
		h & startDay;
		h & awardOwner;
		validate();
	}
};
}
