/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>

/// Value-owned completed HERO cast history. Rejected requests and damage packets
/// never enter this state. Copying a battle branch copies its entire history.
struct DLL_LINKAGE WisdomCombatState
{
	std::map<SpellID, uint32_t> completedCasts;
	int32_t lastCastRound = -1;
	uint8_t consecutiveCastRounds = 0;
	uint32_t priorCasts(SpellID spell) const
	{
		const auto found = completedCasts.find(spell);
		return found == completedCasts.end() ? 0 : found->second;
	}
	int32_t momentumBonusPercent(int32_t round) const
	{
		return lastCastRound >= 0 && (round == lastCastRound || static_cast<int64_t>(round) == static_cast<int64_t>(lastCastRound) + 1)
			? 5 * consecutiveCastRounds : 0;
	}
	void recordCompletedCast(SpellID spell, int32_t round)
	{
		if(!spell.hasValue() || round < 0 || round < lastCastRound)
			throw std::invalid_argument("Invalid completed Wisdom spell cast");
		if(round != lastCastRound)
			consecutiveCastRounds = static_cast<uint8_t>(momentumBonusPercent(round) > 0
				? std::min(4, consecutiveCastRounds + 1) : 1);
		lastCastRound = round;
		auto & count = completedCasts[spell];
		if(count != std::numeric_limits<uint32_t>::max())
			++count;
	}
	void validate() const
	{
		if(lastCastRound < -1 || consecutiveCastRounds > 4
			|| ((lastCastRound == -1) != completedCasts.empty())
			|| ((consecutiveCastRounds == 0) != completedCasts.empty()))
			throw std::runtime_error("Invalid Wisdom combat history");
		for(const auto & [spell, count] : completedCasts)
			if(!spell.hasValue() || count == 0)
				throw std::runtime_error("Invalid Wisdom spell count");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & completedCasts;
		h & lastCastRound;
		h & consecutiveCastRounds;
		if(!h.saving) validate();
	}
};
