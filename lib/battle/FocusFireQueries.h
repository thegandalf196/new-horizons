/*
 * FocusFireQueries.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
class CBattleInfoCallback;
struct SideInBattle;
enum class BattleSide : int8_t;

namespace focusFireQueries
{
/// Saved mark/context validation shared with BattleInfo's enclosing Order checks.
/// Does not create an Order, bind descriptors, or change captured rule identity.
DLL_LINKAGE void validateMarkContext(const CBattleInfoCallback & battle, BattleSide side,
	const SideInBattle & state, bool canonicalOrderRules);
}
