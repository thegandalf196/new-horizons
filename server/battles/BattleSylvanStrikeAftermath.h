/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <cstdint>
class CGameHandler;
class CBattleInfoCallback;
class CStack;
struct BattleAttack;
struct CombatEventPayload;
namespace battle { class Unit; class CUnitState; }
namespace battleSylvanStrikeAftermath
{
bool recordDamageResources(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * victim, battle::CUnitState & preparedVictim, bool ranged, bool spellLike, int64_t recordedDamage, int64_t actualDamage);
void finish(CGameHandler & game, const CBattleInfoCallback & battle, const CStack * attacker,
	const battle::Unit * defender, BattleAttack & packet, const CombatEventPayload & payload,
	battle::CUnitState & attackerState, bool ranged);
}
