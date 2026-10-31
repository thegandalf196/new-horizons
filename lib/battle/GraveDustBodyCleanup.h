/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <vector>
class IBattleState;
class IBattleInfo;
struct BattleStackAttacked;
namespace newHorizonsGraveDust
{
DLL_LINKAGE void validateBodyChanges(const IBattleInfo & battle, const std::vector<BattleStackAttacked> & hits);
/// Prepared native hit flags identify body replacement after Rebirth decisions, not merely post-hit alive state.
DLL_LINKAGE void cleanupBodyChanges(IBattleState & battle, const std::vector<BattleStackAttacked> & hits);
}
