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
DLL_LINKAGE bool protectsDirectTarget(const battle::Unit * unit);
DLL_LINKAGE int shotLuck(const CBattleInfoCallback & battle, const battle::Unit * unit,
	bool shooting);
DLL_LINKAGE void acceptWait(const CBattleInfoCallback & battle, battle::CUnitState & unit);
DLL_LINKAGE void damageFromAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit,
	int64_t & amount, battle::DamageProvenance provenance);
DLL_LINKAGE void receiveAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit);
}
