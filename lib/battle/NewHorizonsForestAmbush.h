/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
class CBattleInfoCallback;
namespace battle { class Unit; class CUnitState; enum class DamageProvenance : uint8_t; }
namespace newHorizonsForestAmbush
{
inline constexpr int FIRST_SHOT_LUCK = 5;
DLL_LINKAGE bool entitled(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE bool hasBattlePolicy(const CBattleInfoCallback & battle);
DLL_LINKAGE bool concealed(const battle::Unit * unit);
/// Sanctuary or Ivor's Forest Ambush: no direct attack of any kind.
DLL_LINKAGE bool protectsFromMelee(const battle::Unit * unit);
/// Also Sylvan Tactics 5 Concealment: no ranged attack, Overwatch or hostile
/// single-target spell. Melee sites use protectsFromMelee.
DLL_LINKAGE bool protectsFromRangedOrSpell(const battle::Unit * unit);
DLL_LINKAGE int shotLuck(const CBattleInfoCallback & battle, const battle::Unit * unit,
	bool shooting);
DLL_LINKAGE void acceptWait(const CBattleInfoCallback & battle, battle::CUnitState & unit);
DLL_LINKAGE void damageFromAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit,
	int64_t & amount, battle::DamageProvenance provenance);
DLL_LINKAGE void receiveAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit);
}
