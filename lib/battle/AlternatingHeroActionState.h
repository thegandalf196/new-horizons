/*
 * AlternatingHeroActionState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

/// Value-only readiness between ordinary hero spells and orders.
/// Callers must exclude Metamagic follow-up actions from recordAcceptedAction.
struct DLL_LINKAGE AlternatingHeroActionState
{
	enum class Action : int32_t
	{
		NONE,
		SPELL,
		ORDER
	};

	Action nextEligibleAction = Action::NONE;
	int32_t empowermentPercent = 0;
	int32_t expiryRound = 0;

	bool operator==(const AlternatingHeroActionState &) const = default;

	/// Returns a matching, unexpired empowerment without changing this state.
	int32_t bonusFor(Action action, int32_t currentRound) const
	{
		validateShape();
		if(currentRound < 0)
			return 0;

		if(action == nextEligibleAction && action != Action::NONE && currentRound <= expiryRound)
			return empowermentPercent;
		return 0;
	}

	/// Records an accepted ordinary spell or order and returns its consumed prior bonus.
	/// Readiness expires inclusively at currentRound + lifetimeRounds. A zero empowerment
	/// consumes any matching prior bonus, then clears readiness.
	int32_t recordAcceptedAction(Action action, int32_t currentRound, int32_t empowerment, int32_t lifetimeRounds = 1)
	{
		validateShape();
		if((action != Action::SPELL && action != Action::ORDER)
			|| currentRound < 0 || empowerment < 0 || lifetimeRounds < 0)
			throw std::invalid_argument("Invalid alternating hero action input");

		if(currentRound > std::numeric_limits<int32_t>::max() - lifetimeRounds)
			throw std::invalid_argument("Alternating hero action expiry round overflows");

		const auto consumedEmpowerment = bonusFor(action, currentRound);
		if(empowerment == 0)
		{
			clearReadiness();
			return consumedEmpowerment;
		}

		nextEligibleAction = action == Action::SPELL ? Action::ORDER : Action::SPELL;
		empowermentPercent = empowerment;
		expiryRound = currentRound + lifetimeRounds;
		return consumedEmpowerment;
	}

	/// Returns a copy with readiness cleared only after its inclusive expiry round.
	/// This helper has no side effects; assign its result when advancing stored state.
	AlternatingHeroActionState clearedIfExpired(int32_t currentRound) const
	{
		validateShape();
		if(currentRound < 0)
			throw std::invalid_argument("Invalid alternating hero action round");

		auto result = *this;
		if(result.nextEligibleAction != Action::NONE && currentRound > result.expiryRound)
			result.clearReadiness();
		return result;
	}

	void validateShape() const
	{
		const bool validAction = nextEligibleAction == Action::NONE
			|| nextEligibleAction == Action::SPELL || nextEligibleAction == Action::ORDER;
		const bool inactiveShape = nextEligibleAction == Action::NONE
			&& empowermentPercent == 0 && expiryRound == 0;
		const bool activeShape = nextEligibleAction != Action::NONE
			&& empowermentPercent > 0 && expiryRound >= 0;
		if(!validAction || (!inactiveShape && !activeShape))
			throw std::runtime_error("Invalid alternating hero action state shape");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & nextEligibleAction;
		h & empowermentPercent;
		h & expiryRound;
		if(!h.saving)
			validateShape();
	}

private:
	void clearReadiness()
	{
		nextEligibleAction = Action::NONE;
		empowermentPercent = 0;
		expiryRound = 0;
	}
};
