/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../bonuses/Bonus.h"
#include <optional>
#include <vector>
namespace battle { class Unit; class CUnitState; }
class IBattleState;
class IBattleInfo;
class CBattleInfoCallback;
namespace newHorizonsSerpentsEmbrace
{
DLL_LINKAGE bool qualifiesAttack(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary);
/// New packet applications require both living, distinct unit identities.
DLL_LINKAGE void validateApplication(const IBattleInfo & battle, uint32_t targetId, const Bonus & bonus);
/// Recognition is cheap; sourceId validates structure/provenance without library access.
/// Exact registered hero identity is checked only at loaded battle admission.
DLL_LINKAGE bool isMarker(const Bonus & bonus) noexcept;
DLL_LINKAGE std::optional<uint32_t> sourceId(const Bonus & bonus);
DLL_LINKAGE Bonus marker(uint32_t sourceUnitId, CreatureID sourceCreature);
DLL_LINKAGE bool blocksVoluntaryMovement(const battle::Unit & target);
/// Only this source's restraint, preserving other Naga/Entangle/classic Bind.
DLL_LINKAGE std::vector<Bonus> releasePlan(const battle::Unit & target, uint32_t sourceUnitId);
/// Bounded existing-unit pass; no current species/owner gate or persistent ledger.
DLL_LINKAGE void releaseBindings(IBattleState & battle, uint32_t sourceUnitId);
/// Original death also retires its linked clone; this changes restraints only.
DLL_LINKAGE void releaseDeathBindings(IBattleState & battle, const battle::CUnitState & source);
}
