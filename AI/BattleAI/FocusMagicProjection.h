/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "AttackPossibility.h"
namespace battle { class CUnitState; }
class CBattleInfoCallback;
class Environment;
class HypotheticBattle;
class StackWithBonuses;
struct BattleAttackInfo;
#include <memory>
#include <optional>
namespace focusMagicAI
{
/// Exact accepted source-local records; imports once, never consumes a charge.
DLL_LINKAGE void restoreAcceptedRecords(StackWithBonuses &, const battle::CUnitState &);
/// Commits one accepted projected strike, then consumes its hard Focus charge.
/// Focus-bearing forecasts require an isolated StackWithBonuses owner.
bool requiresProjection(const CBattleInfoCallback &);
/// Isolated direct callback adaptation; retains the actual visible query source.
std::optional<AttackPossibility> evaluateDirect(const Environment *, const BattleAttackInfo &,
	BattleHex, DamageCache &, const std::shared_ptr<CBattleInfoCallback> &, bool perfectMoment,
	const std::optional<newHorizonsAerialSupremacy::Excursion> &, const dreadCavalryAI::Action *,
	bool validatedApproach, BattleHex approachOrigin);
DLL_LINKAGE void afterAttack(battle::CUnitState &, bool ranged, bool counter, bool physical = true, bool accepted = true, HypotheticBattle * branch = nullptr);
}
