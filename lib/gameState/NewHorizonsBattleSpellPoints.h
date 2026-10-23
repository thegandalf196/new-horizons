/*
 * NewHorizonsBattleSpellPoints.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <array>
#include <cstdint>
#include "../battle/BattleSide.h"
#include "../entities/hero/NewHorizonsAdditionalSpecialties.h"
class CGameState;
class BattleInfo;
namespace newHorizonsHeroes
{
struct DLL_LINKAGE BattleSpellPointEntry
{
	BattleSideArray<std::optional<ManaFurnaceRules>> manaFurnace;
};
/// Validate both sides before attaching a battle or mutating either hero.
DLL_LINKAGE BattleSpellPointEntry validateBattleSpellPointEntry(const CGameState & state, const BattleInfo & battle);
/// Accepted visitor seam only: after validation/localInit and defender-town attachment.
/// Preserves original cancellation snapshots and existing Buffer/drain ordering.
DLL_LINKAGE void applyAcceptedBattleSpellPointEntry(CGameState & state, BattleInfo & battle, const BattleSpellPointEntry & entry);
}
