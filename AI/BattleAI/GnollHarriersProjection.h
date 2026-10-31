/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleHex.h"
#include <cstdint>
class HypotheticBattle;
class AttackPossibility;
namespace battle { class Unit; }
namespace gnollHarriersAI
{
/// Compare the replay owner before its movement, not an already-arrived preview child.
bool replaysSameApproach(const AttackPossibility & captured, const battle::Unit & originalActor);
/// The caller owns an actual ordinary voluntary movement commitment.
void record(HypotheticBattle & battle, uint32_t mover, BattleHex before, BattleHex after);
/// Snapshot copying is not another movement event or an action prefix.
void restoreEffects(HypotheticBattle & battle, const AttackPossibility & captured, bool committedPrefix);
}
