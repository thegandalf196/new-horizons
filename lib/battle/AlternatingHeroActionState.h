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

#include <array>
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
	int32_t lastManaRecoveryRound = -1;
	bool hasConsumedBonus = false;
	/// Right-aligned history of the last three ordinary accepted Hero Actions.
	std::array<Action, 3> recentActions{Action::NONE, Action::NONE, Action::NONE};

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

		recordRecentAction(action);

		const auto consumedEmpowerment = bonusFor(action, currentRound);
		if(consumedEmpowerment > 0)
			hasConsumedBonus = true;
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

	/// True when the last three recorded actions alternate Spell / Order / Spell or Order / Spell / Order.
	bool hasAlternatingSpellOrderSequence() const
	{
		validateShape();
		return (recentActions[0] == Action::SPELL
			&& recentActions[1] == Action::ORDER
			&& recentActions[2] == Action::SPELL)
			|| (recentActions[0] == Action::ORDER
				&& recentActions[1] == Action::SPELL
				&& recentActions[2] == Action::ORDER);
	}

	bool hasRecentActionHistory() const
	{
		return recentActions[0] != Action::NONE
			|| recentActions[1] != Action::NONE
			|| recentActions[2] != Action::NONE;
	}

	void validateShape() const
	{
		const bool validAction = nextEligibleAction == Action::NONE
			|| nextEligibleAction == Action::SPELL || nextEligibleAction == Action::ORDER;
		const bool inactiveShape = nextEligibleAction == Action::NONE
			&& empowermentPercent == 0 && expiryRound == 0;
		const bool activeShape = nextEligibleAction != Action::NONE
			&& empowermentPercent > 0 && expiryRound >= 0;
		bool foundRecentAction = false;
		bool validRecentActions = true;
		for(const auto action : recentActions)
		{
			const bool valid = action == Action::NONE || action == Action::SPELL || action == Action::ORDER;
			if(!valid || (action == Action::NONE && foundRecentAction))
				validRecentActions = false;
			if(action != Action::NONE)
				foundRecentAction = true;
		}
		if(!validAction || (!inactiveShape && !activeShape) || lastManaRecoveryRound < -1 || !validRecentActions)
			throw std::runtime_error("Invalid alternating hero action state shape");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
		{
			validateShape();
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_MEDITATION)
				&& lastManaRecoveryRound != -1)
				throw std::runtime_error("Cannot save Battle Meditation recovery state to an older version");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_SYNTHESIS) && hasConsumedBonus)
				throw std::runtime_error("Cannot save Warcasting consumption history to an older version");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_SEQUENCE) && hasRecentActionHistory())
				throw std::runtime_error("Cannot save Hero Action sequence history to an older format");
		}
		h & nextEligibleAction;
		h & empowermentPercent;
		h & expiryRound;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLE_MEDITATION))
			h & lastManaRecoveryRound;
		else if(!h.saving)
			lastManaRecoveryRound = -1;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MASTER_SYNTHESIS))
			h & hasConsumedBonus;
		else if(!h.saving)
			hasConsumedBonus = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_ACTION_SEQUENCE))
			h & recentActions;
		else if(!h.saving)
			recentActions.fill(Action::NONE);
		if(!h.saving)
			validateShape();
	}

private:
	void recordRecentAction(Action action)
	{
		recentActions[0] = recentActions[1];
		recentActions[1] = recentActions[2];
		recentActions[2] = action;
	}

	void clearReadiness()
	{
		nextEligibleAction = Action::NONE;
		empowermentPercent = 0;
		expiryRound = 0;
	}
};
