/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleHex.h"
#include <optional>
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace battleMovement
{
/// Existing attack-and-return path order; first reachable destination within budget.
DLL_LINKAGE std::optional<BattleHex> meleeReturnDestination(const CBattleInfoCallback & battle,
	const battle::Unit & actor, BattleHex origin, int movementBudget);
}
