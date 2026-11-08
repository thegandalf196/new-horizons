/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
class CBattleInfoCallback;
namespace battle { class Unit; class CUnitState; }
namespace newHorizonsSpecialtyEvents::detail
{
enum class Family { Water, Goblin, WalkingDead, Flies, Swordsman, Wyvern, Mage, Cyclops };
DLL_LINKAGE bool family(const battle::Unit & unit, Family kind);
DLL_LINKAGE bool nativeFamily(const battle::CUnitState & unit, Family kind);
DLL_LINKAGE int coefficient(const CBattleInfoCallback & battle, const battle::Unit & unit,
	const char * heroIdentity, const char * key, bool originalArmy = false);
}
