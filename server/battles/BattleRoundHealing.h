/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class CGameHandler;
class CBattleInfoCallback;
class CStack;
namespace battleRoundHealing
{
void afterRound(CGameHandler & gameHandler, const CBattleInfoCallback & battle);
void beforeTurn(CGameHandler & gameHandler, const CBattleInfoCallback & battle, const CStack * stack);
}
