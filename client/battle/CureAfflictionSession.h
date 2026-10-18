/*
 * CureAfflictionSession.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <utility>

/// GUI-thread Cure callbacks borrow the existing battle identity and casting
/// generation. Modal callbacks never keep an old battle alive between events.
/// Battle is the runtime BattleInterface, or a minimal owner in focused tests.
template<typename Battle>
class CureAfflictionSession
{
	std::weak_ptr<Battle> battle;
	uint64_t castingSession;

	std::shared_ptr<Battle> currentBattle(const std::shared_ptr<Battle> & current) const
	{
		auto result = battle.lock();
		if(!result || result != current || !result->actionsController
			|| result->actionsController->getCastingSession() != castingSession)
			return {};
		return result;
	}

public:
	CureAfflictionSession(const std::shared_ptr<Battle> & owner, uint64_t generation)
		: battle(owner), castingSession(generation)
	{
	}

	template<typename Continue>
	void confirm(const std::shared_ptr<Battle> & current, Continue && afterEnding) const
	{
		const auto owner = currentBattle(current);
		if(!owner || !owner->actionsController->heroSpellcastingModeActive() || !owner->makingTurn())
			return;
		// Preserve Cure's existing order: end the captured targeting session before
		// choice bounds, live Hero/target reacquisition, legality and submission.
		owner->actionsController->endCastingSpell();
		std::forward<Continue>(afterEnding)(*owner);
	}

	void cancel(const std::shared_ptr<Battle> & current) const
	{
		if(const auto owner = currentBattle(current))
			owner->actionsController->endCastingSpell();
	}
};
