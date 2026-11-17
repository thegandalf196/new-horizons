/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../bonuses/Bonus.h"
#include "BattleUnitTurnReason.h"
#include <optional>
#include <span>

namespace battle::timedEffects
{
/// Arithmetic shared with BonusDurationStep; opening round does not age effects.
DLL_LINKAGE bool roundClock(const Bonus & bonus, bool plagueMarker);
DLL_LINKAGE int16_t nextTurns(int16_t turns);
DLL_LINKAGE bool activationContinuation(BattleUnitTurnReason reason);
/// Native STACK_GETS_TURN deliberately also expires on HERO_SPELLCAST.
DLL_LINKAGE bool activationExpires(const Bonus & bonus, BattleUnitTurnReason reason,
	bool beginsActivation, bool gamblerPenalty);
DLL_LINKAGE bool activationEndExpires(const Bonus & bonus, bool noQuarterPenalty);
DLL_LINKAGE bool frozenForfeits(BattleUnitTurnReason reason, bool alive, bool frozen);
DLL_LINKAGE bool frozenThaws(BattleUnitTurnReason reason, bool consumedNormalSlot,
	bool completed, bool alive, bool ghost);
DLL_LINKAGE bool targetMoveBreaks(const Bonus & bonus, bool actuallyMoved,
	std::optional<SpellID> entangle);
struct BindAnchor
{
	bool hasSource = false;
	bool sourceAliveAdjacent = false;
};
/// Classic Bind releases the legacy group only when every parameterized source
/// is absent/separated. An unparameterized pin keeps the group intact.
DLL_LINKAGE bool legacyBindReleases(std::span<const BindAnchor> anchors);
enum class StrikeClock : uint8_t { Attack, OwnAttack, BeingDamaged, IndirectDamage, SequenceEnd };
DLL_LINKAGE bool strikeExpires(const Bonus & bonus, StrikeClock clock);
}
