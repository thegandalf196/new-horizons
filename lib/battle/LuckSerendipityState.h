/*
 * LuckSerendipityState.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

/// Generic Luck's round history, independent of Sylvan's combat-long perk.
struct DLL_LINKAGE LuckSerendipityState
{
	bool enabled = false;
	int32_t round = 0;
	bool previousRoundPositiveLuck = false;
	bool currentRoundPositiveLuck = false;
	bool firstAttackUsed = false;

	bool availableAt(int32_t currentRound) const
	{
		return enabled && round == currentRound && round >= 2
			&& !previousRoundPositiveLuck && !firstAttackUsed;
	}
	void validate() const
	{
		if(round < 0 || (!enabled && (round || previousRoundPositiveLuck || currentRoundPositiveLuck || firstAttackUsed))
			|| (round == 0 && (previousRoundPositiveLuck || currentRoundPositiveLuck || firstAttackUsed))
			|| (round == 1 && previousRoundPositiveLuck))
			throw std::runtime_error("Invalid Luck Serendipity round history");
	}
	void nextRound(int32_t currentRound)
	{
		validate();
		if(!enabled)
			return;
		if(round == std::numeric_limits<int32_t>::max() || currentRound != round + 1)
			throw std::runtime_error("Invalid Luck Serendipity round transition");
		previousRoundPositiveLuck = round >= 1 && currentRoundPositiveLuck;
		currentRoundPositiveLuck = false;
		firstAttackUsed = false;
		round = currentRound;
	}
	void recordStrike(bool ordinaryPhysicalAttack, bool positiveLuck)
	{
		validate();
		if(!enabled || round < 1)
			return;
		firstAttackUsed = firstAttackUsed || ordinaryPhysicalAttack;
		currentRoundPositiveLuck = currentRoundPositiveLuck || positiveLuck;
	}
	void validateTransitionFrom(const LuckSerendipityState & previous, int32_t currentRound) const
	{
		validate();
		previous.validate();
		if(enabled != previous.enabled || round != previous.round
			|| (enabled && round != currentRound)
			|| previousRoundPositiveLuck != previous.previousRoundPositiveLuck
			|| (previous.currentRoundPositiveLuck && !currentRoundPositiveLuck)
			|| (previous.firstAttackUsed && !firstAttackUsed))
			throw std::runtime_error("Invalid Luck Serendipity strike transition");
	}
	bool operator==(const LuckSerendipityState &) const = default;
	template<typename Handler> void validateSerialization(Handler & h) const
	{
		validate();
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_LUCK_SERENDIPITY) && *this != LuckSerendipityState{})
			throw std::runtime_error("Cannot discard Luck Serendipity round history");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_LUCK_SERENDIPITY))
		{
			h & enabled;
			h & round;
			h & previousRoundPositiveLuck;
			h & currentRoundPositiveLuck;
			h & firstAttackUsed;
			if(!h.saving)
				validate();
		}
		else if(!h.saving)
			*this = {};
	}
};
