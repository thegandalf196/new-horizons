/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class AttackPossibility;
class HypotheticBattle;
class BattleHex;
namespace coordinatedMarkAI
{
void completeReturn(AttackPossibility & preview);
void replayReturn(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin);
}
