/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
class CGameHandler;
class CBattleInfoCallback;
class CStack;
namespace battleWaitState
{
/// Called only after authoritative Wait admission and its mastery award.
void accept(CGameHandler & game, const CBattleInfoCallback & battle, const CStack & waiter);
}
