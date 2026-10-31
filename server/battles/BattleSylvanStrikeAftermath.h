/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
class CGameHandler;
class CBattleInfoCallback;
class CStack;
struct BattleAttack;
struct CombatEventPayload;
namespace battle { class Unit; class CUnitState; }
namespace battleSylvanStrikeAftermath
{
void finish(CGameHandler & game, const CBattleInfoCallback & battle, const CStack * attacker,
	const battle::Unit * defender, BattleAttack & packet, const CombatEventPayload & payload,
	battle::CUnitState & attackerState, bool ranged);
}
