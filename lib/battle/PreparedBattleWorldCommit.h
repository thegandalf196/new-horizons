/*
 * PreparedBattleWorldCommit.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../bonuses/CBonusSystemNode.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <boost/noncopyable.hpp>

class CGameState;
class NativeBonusGraphPreparation;
class PreparedReplayAppend;
class PreparedClientBattleReplay;
struct BattleUnitsChanged;
namespace battle
{
class ProjectedBattleWorldArena;

/// Common simulation/replay storage owner, NOT a publication/dispatch route.
/// Takes the actual world shared owner and an already-held CGameState::mutex
/// unique_lock. Construction, preparation, commit AND retirement stay on that
/// lock's owning thread. No lock/token is fabricated by this component.
/// Caller also excludes source/library metadata and Bonus-pointee mutation and
/// reentrancy: the existing client mutex alone does not police every server or
/// library writer. In particular arbitrary original unit value edits are NOT
/// versioned; they must remain excluded from constructor through retirement.
/// Retain original/added graph identities and library through this owner.
/// Before authority use, a separate GameConnection producer must preencode ALL
/// recipients for the exact packet/version/connection set without sending and
/// preserve that set through commit. No such producer is supplied or implied here.
class DLL_LINKAGE PreparedBattleWorldCommit final : boost::noncopyable
{
public:
	PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
		std::unique_lock<std::shared_mutex> exclusiveAccess, const BattleUnitsChanged & packet);
	/// Transfer the SAME preacquired state->graph interval after recipient
	/// encoding. Null/wrong-thread sources refuse without consuming them.
	PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
		std::unique_lock<std::shared_mutex> exclusiveAccess, const BattleUnitsChanged & packet,
		std::unique_ptr<NativeBonusGraphPreparation> & preparation);
	~PreparedBattleWorldCommit();
	/// Preparation-only view; do not retain references beyond this owner.
	const ProjectedBattleWorldArena & projected() const;
	/// Immutable packet for a future recipient encoder. Existing serializers must
	/// work on their own packet copies; never mutate this owner's packet via alias.
	const BattleUnitsChanged & packet() const noexcept;
	/// Complete exact mutable closure is caller-owned, never const-cast here.
	/// Allocates all participants before graph capacity/freshness preparation.
	void prepare(const TNodesVector & mutableAuthority);
	void prepare(const TNodesVector & mutableAuthority, const TCNodesVector & readOnlyAuthority);
	bool readyToCommit() const noexcept;
	/// All refusal gates precede graph CAS. After successful CAS, only no-query
	/// preallocated value/root/sidecar/replay tails run, with no rollback or release.
	/// Simulation storage ONLY: does not send, dispatch, reconcile or notify UI.
	bool commitSimulation() noexcept;
private:
	friend class ::PreparedClientBattleReplay;
	// Only the typed client entry may adopt its existing exact replay plan.
	PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
		std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet,
		std::unique_ptr<NativeBonusGraphPreparation> & preparation,
		std::unique_ptr<PreparedReplayAppend> & replay);
	struct Data;
	std::unique_ptr<Data> impl;
};
}
