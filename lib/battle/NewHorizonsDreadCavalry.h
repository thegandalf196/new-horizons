/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleUnitTurnReason.h"
#include "DreadCavalryState.h"
#include <optional>
class CBattleInfoCallback;
class IBattleInfo;
namespace battle { class Unit; }
namespace newHorizonsDreadCavalry
{
/// firstVictim is an already-latched genuine post-Rebirth melee destruction receipt;
/// subsequent resurrection never revokes it. Caller owns that event qualification.
/// Pure proposal from the current resolved world. remaining is original allowance
/// minus actual committed cost; sharedCap is remaining for Pursuit, 2 for
/// Opportunist-only, otherwise0. Vanish is an independent half-Speed grant.
/// Read-only actor-handoff eligibility. The caller owns previous-unit lookup,
/// pending-state checks and mutation; no new battle interface or lifecycle.
DLL_LINKAGE bool abandonsPendingOnActorHandoff(const IBattleInfo & battle,
	uint32_t nextActor, BattleUnitTurnReason reason);
DLL_LINKAGE std::optional<battle::DreadCavalryState> grantAfterMelee(
	const CBattleInfoCallback & battle, const battle::Unit * source, uint32_t firstVictim,
	BattleSide originalController, int remaining, int sharedCap, int vanish);
/// Revalidates an already admitted action without requiring its retired pending
/// marker (native Rebirth may clear it between ordinary multiple attacks).
DLL_LINKAGE bool acceptedBlowStillLegal(const CBattleInfoCallback & battle,
	const battle::Unit * source, const battle::Unit * target, uint32_t originalActor,
	const battle::DreadCavalryState & accepted);
DLL_LINKAGE BattleUnitTurnReason continuationReason(const battle::Unit * source);
DLL_LINKAGE bool family(const battle::Unit * source);
DLL_LINKAGE std::optional<int> entitlement(const CBattleInfoCallback & battle, const battle::Unit * source);
/// Loaded Crisis return actors may be suspended; ordinary input requires active.
DLL_LINKAGE bool hasPending(const CBattleInfoCallback & battle, const battle::Unit * source, bool requireActive = true);
/// Cheap pending restrictions only; callers retain all native attack geometry.
DLL_LINKAGE bool permitsTarget(const CBattleInfoCallback & battle,
	const battle::Unit * source, const battle::Unit * target);
DLL_LINKAGE bool hasRawPending(const battle::Unit * source);
DLL_LINKAGE int outputPercent(const CBattleInfoCallback & battle, const battle::Unit * source);
DLL_LINKAGE int movementRemaining(const CBattleInfoCallback & battle, const battle::Unit * source);
}
