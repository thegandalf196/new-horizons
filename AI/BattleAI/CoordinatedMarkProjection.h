/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class AttackPossibility;
class DamageCache;
class HypotheticBattle;
class BattleHex;
namespace coordinatedMarkAI
{
void prepareReturn(AttackPossibility & preview, int movementBudget, DamageCache & cache);
void completeReturn(AttackPossibility & preview);
bool replayMovement(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin);
void replayReturn(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin);
}
