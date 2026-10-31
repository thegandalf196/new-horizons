/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsToxicSting.h"
#include <vector>
class CGameHandler;
class BattleProcessor;
struct BattleStackAttacked;
namespace battleToxicSting
{
std::optional<newHorizonsToxicSting::Plan> prepare(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, const std::vector<BattleStackAttacked> & hits);
void apply(CGameHandler & game, BattleProcessor & processor, const CBattleInfoCallback & battle,
	const std::optional<newHorizonsToxicSting::Plan> & prepared);
}
