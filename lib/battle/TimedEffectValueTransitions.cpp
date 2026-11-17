/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "TimedEffectValueTransitions.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace battle::timedEffects
{
bool roundClock(const Bonus & bonus, bool plagueMarker)
{
	return Bonus::NTurns(&bonus) && !plagueMarker;
}
int16_t nextTurns(int16_t turns)
{
	// Preserve native si16 duration arithmetic, including every sentinel. The
	// promoted subtraction cannot overflow; C++20 narrowing is modulo 2^16.
	return static_cast<int16_t>(turns - 1);
}
bool activationContinuation(BattleUnitTurnReason reason)
{
	return reason == BattleUnitTurnReason::CRISIS_ORDER || reason == BattleUnitTurnReason::CRISIS_RESUME
		|| reason == BattleUnitTurnReason::ACTION_REJECTED || reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION || reason == BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION;
}
bool activationExpires(const Bonus & bonus, BattleUnitTurnReason reason,
	bool beginsActivation, bool gamblerPenalty)
{
	if(activationContinuation(reason)) return false;
	if(beginsActivation && (Bonus::UntilNextCreatureActivation(&bonus) || gamblerPenalty)) return true;
	return reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND
		&& Bonus::UntilGetsTurn(&bonus) && !gamblerPenalty;
}
bool activationEndExpires(const Bonus & bonus, bool noQuarterPenalty)
{
	return Bonus::UntilActivationEnds(&bonus) && !noQuarterPenalty;
}
bool frozenForfeits(BattleUnitTurnReason reason, bool alive, bool frozen)
{
	return reason == BattleUnitTurnReason::TURN_QUEUE && alive && frozen;
}
bool frozenThaws(BattleUnitTurnReason reason, bool consumedNormalSlot,
	bool completed, bool alive, bool ghost)
{
	return reason == BattleUnitTurnReason::TURN_QUEUE && consumedNormalSlot && completed && alive && !ghost;
}
bool targetMoveBreaks(const Bonus & bonus, bool actuallyMoved, std::optional<SpellID> entangle)
{
	return actuallyMoved && entangle && bonus.type == BonusType::BIND_EFFECT
		&& bonus.source == BonusSource::SPELL_EFFECT && bonus.sid == BonusSourceID(*entangle);
}
bool legacyBindReleases(std::span<const BindAnchor> anchors)
{
	return !anchors.empty() && std::ranges::all_of(anchors, [](const BindAnchor & anchor)
	{
		return anchor.hasSource && !anchor.sourceAliveAdjacent;
	});
}
bool strikeExpires(const Bonus & bonus, StrikeClock clock)
{
	switch(clock)
	{
	case StrikeClock::Attack: return Bonus::UntilAttack(&bonus);
	case StrikeClock::OwnAttack: return Bonus::UntilOwnAttack(&bonus);
	case StrikeClock::BeingDamaged: return Bonus::UntilBeingAttacked(&bonus);
	case StrikeClock::IndirectDamage: return Bonus::UntilTakingIndirectDamage(&bonus);
	case StrikeClock::SequenceEnd: return Bonus::untilAfterAttackSequence(&bonus);
	}
	return false;
}
}
