/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <array>
#include <optional>
#include "../bonuses/Bonus.h"

class BattleInfo;
class CBattleInfoCallback;
namespace battle { class Unit; }
class CGHeroInstance;
namespace battle { class CUnitState; }
namespace newHorizonsMetamagic
{
DLL_LINKAGE std::optional<Bonus> entryAttunement(const CBattleInfoCallback & callback, const battle::Unit & unit);
/// Immutable identity source plus the current branch's explicitly captured points.
DLL_LINKAGE std::optional<Bonus> entryAttunement(const CBattleInfoCallback & callback,
	const battle::Unit & unit, int32_t points);
DLL_LINKAGE void advanceRound(BattleInfo & battle);
/// Unpublished fresh-battle setup only. Never call during load/local reinit.
DLL_LINKAGE void captureInitialMarks(BattleInfo & battle);
/// Brand-new entity initialization only; never a controller change or revival.
/// A native SUMMONED entity (including clones) gets its own entry seeds.
DLL_LINKAGE void seedSummonedMarks(battle::CUnitState & state,
	const std::array<const CGHeroInstance *, 2> & heroes);
}
