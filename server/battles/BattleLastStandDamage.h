/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
class CBattleInfoCallback;
struct BattleAttackInfo;
struct BattleAttack;
struct BattleStackAttacked;
namespace battle { class CUnitState; }
namespace battleLastStandDamage
{
/// Authoritative pre-casualty Last Stand packet/state preparation. No RNG or publication.
bool prepare(const CBattleInfoCallback &, const BattleAttackInfo &, BattleAttack &,
	BattleStackAttacked &, battle::CUnitState &, std::array<bool, 2> & usedThisAttack, bool retaliation);
}
