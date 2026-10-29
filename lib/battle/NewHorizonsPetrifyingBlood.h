/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <cstdint>
class CBattleInfoCallback;
namespace battle { class Unit; class CUnitState; }
namespace newHorizonsPetrifyingBlood
{
DLL_LINKAGE bool eligiblePair(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit & basilisk);
DLL_LINKAGE bool isPetrified(const battle::Unit & unit);
/// The first actual enemy physical melee damage receipt spends this unit's round attempt.
DLL_LINKAGE bool qualifies(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::CUnitState & basilisk, int64_t actualDamage, bool ranged, bool physical);
/// Native cast-at admission and favorable proc chance in basis points; no random draw.
/// Downstream resistance and adverse one-shot reroll consumption remain authoritative.
DLL_LINKAGE int chanceBasisPoints(const CBattleInfoCallback & battle,
	const battle::Unit & basilisk, const battle::Unit & attacker);
}
