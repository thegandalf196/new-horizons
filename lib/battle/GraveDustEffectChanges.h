/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <vector>
class IBattleInfo;
class BattleInfo;
struct SetStackEffect;
namespace battle { class Unit; }
namespace newHorizonsGraveDust
{
enum class EffectOperation { REMOVE, UPDATE, ADD };
/// Record-only fast path; no unit or bonus-graph query for an absent Dust change.
DLL_LINKAGE bool containsEffect(const std::vector<Bonus> & records);
DLL_LINKAGE bool containsEffect(const SetStackEffect & effects);
/// Declared-input admission at one vector start; ordered replay also checks its installed result.
DLL_LINKAGE void validateMutation(const IBattleInfo & battle, const battle::Unit & unit,
	const std::vector<Bonus> & records, EffectOperation operation);
/// Canonical current raw group admission, including hidden/duplicate companions.
DLL_LINKAGE void validateInstalledGroup(const battle::Unit & unit);
/// Legacy declared-group check; read-only IBattleInfo alone does not authorize native replay.
DLL_LINKAGE void validateEffectChanges(const IBattleInfo & battle, const SetStackEffect & effects);
/// Actual authoritative native receiver, never a player callback underlying-world import.
DLL_LINKAGE void validateEffectChanges(const BattleInfo & battle, const SetStackEffect & effects);
}
