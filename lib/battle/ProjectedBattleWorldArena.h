/*
 * ProjectedBattleWorldArena.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "Unit.h"
#include "UnitUpdateAdmission.h"
#include "HeroCommand.h"
#include "ReducedExtraActivationState.h"
#include "ProjectedUnitRemoval.h"
#include "AccessibilityInfo.h"
#include "../bonuses/CBonusSystemNode.h"
#include "../bonuses/ProjectedExportRemoval.h"
#include "../bonuses/ProjectedDurationChange.h"
#include "../bonuses/RecipientBonusOverlay.h"
#include <memory>
#include <set>
#include <boost/noncopyable.hpp>

class BattleInfo;
class CStack;
class ProjectedBonusRoster;
class UnitChanges;
struct BattleUnitsChanged;
namespace newHorizonsCrisisCommand { struct State; }
namespace battle
{
class CUnitStateDetached;
class CUnitState;
class ProjectedUnitEnvironmentState;
class PreparedBattleUnitRoots;
class PreparedBattleWorldCommit;
struct BattleUnitSidecarFields;
class PreparedBattleUnitSidecars;

/// Stable private identity/value/query arena, NOT an authoritative packet route.
/// Caller retains the original world and immutable source/library metadata under
/// its REAL exclusive/nonreentrant ownership interval. No lease is created here.
/// Original values/bonus pointees may not change through external aliases.
/// Failure abandons ALL private prefixes. No partial live transfer or rollback.
/// The arena owns graph AND its optional finalized storage until retirement;
/// neither an owning graph plan nor a graph-only commit API can escape it.
class DLL_LINKAGE ProjectedBattleWorldArena final : boost::noncopyable
{
public:
	explicit ProjectedBattleWorldArena(BattleInfo & original);
	~ProjectedBattleWorldArena();
	size_t size() const;
	uint32_t nextUnitId() const;
	const CUnitStateDetached & unitAt(size_t occurrence) const;
	const CStack & originalAt(size_t occurrence) const;
	/// Exact projected serializer; never ordinary-provider detached save.
	JsonNode snapshotAt(size_t occurrence);
	/// Mutate this private canonical slot only; no UPDATE expiry/packet lifecycle.
	/// Use a disposable owner when deriving a scratch damage snapshot.
	JsonNode damagePhysicalAndSnapshotAt(size_t occurrence, int64_t & damage);
	/// All current graph records at the original fighting hero; no hero is empty.
	/// Requires ATTACKER/DEFENDER and the existing immutable native source lease.
	TConstBonusListPtr projectedHeroBonuses(BattleSide side, bool unstacked) const;
	void installRecipientOverlaysAt(size_t occurrence, std::vector<recipientBonusOverlay::Layer> layers);
	std::optional<std::vector<recipientBonusOverlay::Layer>> removeRecipientOverlaysMatchingAt(
		size_t occurrence, const CSelector & selector);
	void replayRecipientOverlaysAt(size_t occurrence, std::vector<recipientBonusOverlay::Layer> layers);
	bool hasUnresolvedRecipientOverlays() const;
	const CUnitStateDetached * findUnit(uint32_t id, bool onlyAlive = false) const;
	const ProjectedBonusRoster & roster() const;
	/// Exact physical export owner, not an effective recipient selection. Bonus
	/// pointees/configuration aliases must remain immutable under the real lease.
	void addExportedBonusAt(size_t occurrence, const std::shared_ptr<Bonus> & record);
	void removeExportedBonusAt(size_t occurrence, const std::shared_ptr<Bonus> & record);
	void expireExportedBonusesAt(size_t occurrence, const CSelector & selector);
	/// Internal disposable record-replay fork only. All references borrow this owner.
	/// Caller validates exact source/origin; do not publish or journal unrecorded edits.
	CUnitState & effectQueryStateAt(size_t occurrence);
	const BonusList & exportedBonusesAt(size_t occurrence) const;
	void refreshExportedBonusesAt(size_t occurrence, const Bonus & value);
	std::vector<ProjectedExportRemoval> removeExportedBonusesRecordingAt(size_t occurrence, const CSelector & selector);
	void replayExportedRemovals(const std::vector<ProjectedExportRemoval> & receipts);
	/// Query-only value synchronization: no packet, expiry or lifecycle is run.
	void copyQueryValuesAt(size_t occurrence, const CUnitState & state);
	void restoreQuerySnapshotAt(size_t occurrence, const JsonNode & state);
	std::vector<ProjectedDurationChange> reduceExportedDurationsAt(size_t occurrence, const CSelector & selector);
	void replayDurationChanges(const std::vector<ProjectedDurationChange> & changes);
	void copyEnvironmentValues(const ProjectedUnitEnvironmentState & values);
	const IUnitEnvironment & environment() const;
	AccessibilityInfo accessibility() const;
	/// Complete scalar ADD initialization, not packet-level Crisis/Bloodrage.
	const CUnitStateDetached & addUnit(uint32_t id, const JsonNode & data);
	enum class UpdateDecision { APPLY, IGNORE_TIME_STOP, IGNORE_OCCUPIED_RESURRECTION };
	/// Missing UPDATE throws BEFORE bonus/placement/JSON reads. No load occurs.
	UpdateDecision classifyUpdate(uint32_t id, int64_t healthDelta);
	/// Ordered scalar UPDATE, not packet-level Crisis/Bloodrage/replay/publication.
	UpdateDecision updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta);
	/// Complete scalar REMOVE including completion-dependent activation/Protect.
	ProjectedUnitRemoval::Result removeUnit(uint32_t id);
	const std::vector<HeroOrderState> & orderStates(BattleSide side) const;
	const ReducedExtraActivationState & reducedActivation(BattleSide side) const;
	/// Ordinary entry-list program, NOT Phoenix/Rebirth consumption wire dispatch.
	/// Captures ALL candidates before entries, then original post-entry sidecars.
	void applyUnitChanges(const std::vector<UnitChanges> & changes);
	/// Full wire-envelope decision program, still PRIVATE preparation only.
	/// Bound battle identity must match the packet; caller owns real exclusion.
	void applyPacket(const BattleUnitsChanged & packet);
	bool rebirthChainUsed(BattleSide side) const;
	bool phoenixSparkUsed(BattleSide side) const;
	const newHorizonsCrisisCommand::State & crisisState() const;
	const std::set<uint32_t> & bloodrageDeaths() const;
	bool firstBloodUsed() const;
	int bloodrageDamage(BattleSide side) const;
	/// Existing value/edge/clone kernel ONLY; full REMOVE sidecars still required.
	ProjectedUnitRemoval::Result removeValuesAndEdges(uint32_t id);
	size_t retainedQueryCount() const;
	TCNodesVector capturedGraphNodes() const;
	TNodesVector ownedGraphNodes() const;
	/// Capacity preparation can allocate; caller supplies exact mutable closure.
	/// This freezes the arena; no live value/root/graph commit is offered here.
	void prepareGraphStorage(const TNodesVector & mutableAuthority);
	bool graphStorageReady() const noexcept;
private:
	friend class PreparedBattleUnitRoots;
	friend class PreparedBattleWorldCommit;
	friend struct BattleUnitSidecarFields;
	friend class PreparedBattleUnitSidecars;
	struct Data;
	std::unique_ptr<Data> impl;
};
}
