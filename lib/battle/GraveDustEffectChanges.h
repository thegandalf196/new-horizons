/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <vector>
class IBattleInfo;
struct SetStackEffect;
namespace battle { class Unit; }
namespace newHorizonsGraveDust
{
enum class EffectOperation { REMOVE, UPDATE, ADD };
/// Record-only fast path; no unit or bonus-graph query for an absent Dust change.
DLL_LINKAGE bool containsEffect(const std::vector<Bonus> & records);
DLL_LINKAGE bool containsEffect(const SetStackEffect & effects);
/// Nonwriting simulation of this exact native operation. Complete group or empty after every call.
DLL_LINKAGE void validateMutation(const IBattleInfo & battle, const battle::Unit & unit,
	const std::vector<Bonus> & records, EffectOperation operation);
/// All affected targets and ordered native calls are checked before any packet mutation/broadcast.
DLL_LINKAGE void validateEffectChanges(const IBattleInfo & battle, const SetStackEffect & effects);
}
