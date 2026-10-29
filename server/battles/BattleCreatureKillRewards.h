/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <vector>
class CBattleInfoCallback;
struct BattleStackAttacked;
class CStack;
namespace battle { class CUnitState; class Unit; }
namespace battleCreatureKillRewards
{
/// Existing Pursuit receipt uses packet killing flags before application. This
/// is intentionally different from Dread Cavalry's resolved, latched event.
void recordPredictedEnemyDestruction(const CBattleInfoCallback & battle, const battle::Unit * actor,
	const std::vector<BattleStackAttacked> & hits, bool * destroyedEnemyOut);

bool chainGateKillQualifies(const CBattleInfoCallback & battle, const CStack * actor,
	const std::vector<BattleStackAttacked> & hits);
void finishStrike(const CBattleInfoCallback & battle, battle::CUnitState & actor,
	const std::vector<BattleStackAttacked> & hits, bool ranged, bool counter, bool physical);
}
