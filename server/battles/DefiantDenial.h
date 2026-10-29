/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
class CGameHandler;
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../lib/battle/ArmorerDefiantState.h"
namespace battleDefiant
{
bool consumeGaze(CGameHandler & handler, const CBattleInfoCallback & battle,
	const battle::Unit & opponent, const battle::Unit & retaliator, bool ranged,
	const newHorizonsAerialSupremacy::Excursion * aerial = nullptr);
bool consume(CGameHandler & handler, const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, newHorizonsArmorer::DefiantDenialCause cause);
}
