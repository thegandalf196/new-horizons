/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
class CBattleInfoCallback;
struct Bonus;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsPassiveSpecialties
{
DLL_LINKAGE int casualtyPercent(int32_t entry, int32_t currentOriginalCount);
DLL_LINKAGE int boneFormationDefense(int32_t count);
DLL_LINKAGE bool magicalDamageImmune(const battle::Unit & unit);
DLL_LINKAGE bool isDynamicPassiveBonus(const Bonus & bonus);
DLL_LINKAGE bool hasDynamicDamagePassive(const battle::Unit & unit);
DLL_LINKAGE int casualtyDamagePercent(const BattleAttackInfo & attack);
/// Deliberate creature attacks only; do not use for retaliation/collateral/spells.
DLL_LINKAGE bool permitsCreatureAttackTarget(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * target);
}
