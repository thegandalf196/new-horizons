/*
 * InitialExportWorld.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/ProjectedUnitEnvironmentState.h"

/// AI-owned ordered graph/value program over canonical graph-bound recipients.
/// Not a CBattleInfoCallback, legality validator, hidden-info adapter or commit.
/// Caller retains the native source/library and immutable configuration aliases
/// under its actual lease through this owner, all forks and every borrowed view.
class InitialExportWorld final
{
	struct Data;
	std::unique_ptr<Data> impl;
public:
	struct Source
	{
		size_t occurrence = 0;
		uint32_t unitId = 0;
		const CStack * original = nullptr;
	};
	class ExportHandle
	{
		struct Identity;
		std::shared_ptr<const Identity> identity;
		explicit ExportHandle(std::shared_ptr<const Identity> identity);
		friend class InitialExportWorld;
		friend struct Data;
	public:
		ExportHandle() = default;
	};
	enum class Expiry { UNTIL_ATTACK, UNTIL_OWN_ATTACK, UNTIL_BEING_ATTACKED };

	explicit InitialExportWorld(BattleInfo & original);
	~InitialExportWorld();
	InitialExportWorld(const InitialExportWorld &) = delete;
	InitialExportWorld & operator=(const InitialExportWorld &) = delete;
	size_t size() const;
	Source sourceAt(size_t occurrence) const;
	/// Already prepared originating EXPORT metadata, not an effect specification
	/// needing refresh/stamping. Copy preserves every Bonus field; configuration
	/// object aliases remain immutable, as required by ProjectedBonusGraph.
	ExportHandle add(Source source, const Bonus & record);
	void remove(const ExportHandle & handle);
	void expire(Source source, Expiry expiry);
	/// Snapshot is branch capture, NOT a native UPDATE or expiry event.
	void copyQueryValues(Source source, const battle::CUnitState & state);
	void recordQuerySnapshot(Source source);
	/// Selector runs now on canonical exports and explicit recipient layer
	/// records, never inferring exports from an effective list. Replay retains
	/// exact native removals and owned replacement layers, not the selector.
	void removeMatching(Source source, const CSelector & selector);
	void reduceDurations(Source source, const CSelector & selector);
	void copyEnvironmentValues(const battle::ProjectedUnitEnvironmentState & values);
	void recordEnvironmentSnapshot(const battle::ProjectedUnitEnvironmentState & values);
	bool hasMovementInvariantSources() const;
	/// Native UPDATE pre-values are distinct from scratch query synchronization.
	battle::ProjectedBattleWorldArena::UpdateDecision updateUnitFrom(Source source,
		const JsonNode & before, const JsonNode & after, int64_t healthDelta);
	/// Native first-ID scalar UPDATE, not packet-level sidecar/action completion.
	battle::ProjectedBattleWorldArena::UpdateDecision updateUnit(uint32_t id, const JsonNode & state, int64_t healthDelta);
	/// Replays the ENTIRE ordered program from the same immutable native origin.
	/// A branch-local new handle cannot be used in a sibling missing its ADD.
	std::unique_ptr<InitialExportWorld> fork() const;
	/// Borrowed provider-bound view: must not outlive this owner/source lease.
	const battle::CUnitStateDetached & unitAt(size_t occurrence) const;
	JsonNode snapshotAt(size_t occurrence);
	/// Replay the recorded program on a canonical private fork, then retire it.
	/// Record copied query/environment values BEFORE later ordered operations;
	/// unrecorded inputs or operations which observed them reject this scratch.
	/// Parent/program/expiry are unchanged; damage is native in/out on success.
	JsonNode snapshotAfterPhysicalDamage(Source source, int64_t & damage) const;
	TConstBonusListPtr projectedHeroBonuses(BattleSide side, bool unstacked) const;
	/// Exact recipient query history, never a native originating EXPORT.
	void installRecipientOverlays(Source source, std::vector<recipientBonusOverlay::Layer> layers);
	bool hasUnresolvedRecipientOverlays() const;
	/// Explicit recipient-model injury needs a complete recorded query program;
	/// this proves replay ownership, NOT originating export/publication authority.
	bool hasRecordedRecipientInjuryInputs() const;
};
