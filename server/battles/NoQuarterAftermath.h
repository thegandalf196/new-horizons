/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <vector>
class CGameHandler;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct BattleStackAttacked;
namespace noQuarterAftermath
{
/// Existing completed-hit phase: complete status, linked Morale and feedback.
void apply(CGameHandler & handler, const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, const std::vector<BattleStackAttacked> & hits);
}
