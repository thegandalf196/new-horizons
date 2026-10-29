/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
namespace battle { class Unit; class CUnitState; }
class CBattleInfoCallback;
namespace newHorizonsSoulHarvest
{
inline constexpr int GHOSTS_PER_CREATURE_KILLED = 1;
/// Captures entitlement before damage removes effects or changes controller state.
DLL_LINKAGE bool qualifies(const CBattleInfoCallback & battle, const battle::Unit & actor,
	const battle::Unit & victim, bool physical);
/// Count only this physical hit's casualties; a ready Rebirth excludes only a
/// whole-stack death, not partial casualties in a still-living stack.
DLL_LINKAGE int32_t casualtiesAfterHit(const battle::Unit & victim,
	int32_t countBeforeHit, bool willRebirth);
/// Grant the full qualified casualty total once after the blow's entire hit set.
/// ONE_BATTLE Overheal never resurrects a dead actor or changes saved old counts.
DLL_LINKAGE bool grant(battle::CUnitState & actor, int64_t killedCreatures);
}
