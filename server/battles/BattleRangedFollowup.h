/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <string>
class CGameHandler;
class CBattleInfoCallback;
class CGHeroInstance;
namespace battle { class Unit; }
namespace battleRangedFollowup
{
void publish(const CBattleInfoCallback & battle, CGameHandler & handler, const battle::Unit * unit, int percent);
void log(const CBattleInfoCallback & battle, CGameHandler & handler, const battle::Unit * unit, std::string text);
// Caller has already validated the existing Master Gunner grant conditions.
void grant(const CBattleInfoCallback & battle, CGameHandler & handler, const battle::Unit * unit,
	const CGHeroInstance * hero, int ordinaryPercent);
}
