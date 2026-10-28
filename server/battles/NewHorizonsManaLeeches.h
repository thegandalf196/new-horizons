/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/NewHorizonsManaLeeches.h"
#include <vector>
class CGameHandler;
struct BattleStackAttacked;
namespace newHorizonsManaLeeches
{
struct Resolution
{
	Transfer amounts;
	BattleID battleID;
	ObjectInstanceID beneficiary;
	ObjectInstanceID victim;
};
Resolution prepare(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary,
	const std::vector<BattleStackAttacked> & hits, bool sequenceSpent);
void apply(CGameHandler & handler, const Resolution & resolution, bool * sequenceSpent);
}
