/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/NewHorizonsLivingInferno.h"
class CGameHandler;
namespace newHorizonsLivingInferno
{
void publish(CGameHandler & handler, const CBattleInfoCallback & battle,
	const Trail & trail, BattleHex finalPosition);
}
