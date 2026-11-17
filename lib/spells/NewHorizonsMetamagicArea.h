/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../battle/BattleHexArray.h"
#include <vector>
class CSpell;
class ServerCallback;
class CBattleInfoCallback;
namespace spells { class Mechanics; }
namespace newHorizonsMetamagic
{
DLL_LINKAGE bool conventionalArea(const CSpell &, int32_t mastery);
DLL_LINKAGE std::vector<int> widenedRings(const spells::Mechanics &, std::vector<int> rings);
/// Uses only the already admitted, existing footprint; grants no target permission.
DLL_LINKAGE void breakMatter(ServerCallback &, const CBattleInfoCallback &,
	const spells::Mechanics &, const BattleHexArray & existingArea);
}
