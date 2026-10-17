/*
 * ProjectedBonusGraph.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "BonusGraphQuery.h"
#include "PreparedBonusGraphStorage.h"
#include "ProjectedBonusView.h"
#include "ProjectedBonusInvalidations.h"
#include "ProjectedDurationChange.h"
#include "ProjectedExportRemoval.h"
#include "RecipientBonusOverlay.h"
#include <map>

class ProjectedBonusRoster;
class JsonNode;
class BattleInfo;
namespace battle { class CUnitState; class CUnitStateDetached; class Unit; class PreparedStackStateTransfer; }

/// Query/preparation owner ONLY, not a live commit or UPDATE route. Caller holds
/// all original/projected lifetimes and exclusive world lease. Shared Bonus
/// payloads are immutable; replace values by copies, never edit shared pointees.
/// No named/list/value cache or generation counter is used for projected queries.
class DLL_LINKAGE ProjectedBonusGraph final : public BonusGraphQuery, boost::noncopyable
{
public:
	using Values = PreparedBonusGraphStorage::Values;
	void capture(const CBonusSystemNode & node);
	TCNodesVector capturedNodes() const;
	const Values & values(const CBonusSystemNode & node) const;
	void replace(const CBonusSystemNode & node, Values value);
	void bind(const CStack & original, const battle::CUnitState & unit, const ProjectedBonusRoster & roster);
	/// Opt-in physical-node identity only; ordinary bind still rejects equal IDs.
	/// Explicit cold node: no native graph edges/base/battle; null means NO battle.
	void bindColdOccurrence(const CStack & original, const battle::CUnitState & unit,
		const ProjectedBonusRoster & roster, const BattleInfo * battle);
	void appendColdOccurrence(const CStack & original, const battle::CUnitState & unit,
		ProjectedBonusRoster & roster, const BattleInfo * battle);
	const std::vector<ProjectedBonusInvalidations::Event> & invalidationEvents() const;
	void bindOccurrence(const CStack & original, const battle::CUnitState & unit, const ProjectedBonusRoster & roster);
	/// Stable roster object, invalidates prior orderedUnits spans. On ANY failure
	/// abandon the entire private world; no graph/roster publication or rollback.
	void appendOccurrence(const CStack & original, const battle::CUnitState & unit, ProjectedBonusRoster & roster);
	void requireBoundUnit(const battle::Unit & unit) const;
	const ProjectedBonusView & view(const CBonusSystemNode & node);
	/// Private staging edits preserve legacy exported/materialized propagation.
	/// Failure poisons this private owner; originals stay unchanged. Discard it.
	void addExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus);
	void removeExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus);
	/// Forecast value receipts, NOT a native publication/invalidation journal.
	std::vector<ProjectedDurationChange> reduceDurationsRecording(const CBonusSystemNode & source, const CSelector & selector);
	void replayDurationChanges(const std::vector<ProjectedDurationChange> & changes);
	/// Exact node-first exported snapshot and ordered child recursion.
	/// Failure poisons private staging; no live graph is changed.
	void removeBonusesRecursive(const CBonusSystemNode & root, const CSelector & selector);
	std::vector<ProjectedExportRemoval> removeBonusesRecursiveRecording(
		const CBonusSystemNode & root, const CSelector & selector);
	/// Legacy advancing invalidation, including unchanged local lists.
	void nodeHasChanged(const CBonusSystemNode & root);
	void attachTo(CBonusSystemNode & node, CBonusSystemNode & parent);
	void attachToSource(const CBonusSystemNode & node, const CBonusSystemNode & parent);
	void detachFrom(CBonusSystemNode & node, CBonusSystemNode & parent);
	void detachFromSource(const CBonusSystemNode & node, const CBonusSystemNode & parent);
	void detachFromAll(CBonusSystemNode & node);
	void requireBoundUnit(const CStack & original, const battle::CUnitState & unit) const;
	/// A larger preparation owner failed after private values changed.
	void abandonPreparation() noexcept { poisoned = true; }
	/// Preserve authored sequential order after no-hook value edits. Queries
	/// auto-synchronize a sole pending form, never guess several edits' order.
	void synchronizeForm(const CStack & original);
	/// Call only after world-level UPDATE ignore/occupancy checks. On load
	/// failure discard this owner AND its possibly partially loaded private unit.
	void loadBoundState(const CStack & original, battle::CUnitStateDetached & state, const JsonNode & data);
	TConstBonusListPtr query(const CBonusSystemNode & node, const CSelector & selector, bool unstacked);
	/// Query-only recipient layers, NOT originating exports or native events.
	/// Installing these permanently excludes this owner from native publication.
	void installRecipientOverlays(const CStack & original, std::vector<recipientBonusOverlay::Layer> layers);
	/// Recipient-local mutation, without inventing propagation/export ownership.
	std::optional<std::vector<recipientBonusOverlay::Layer>> removeRecipientOverlaysMatching(
		const CStack & original, const CSelector & selector);
	void replayRecipientOverlays(const CStack & original, std::vector<recipientBonusOverlay::Layer> layers);
	bool hasUnresolvedRecipientOverlays() const;

	const BonusList & local(const CBonusSystemNode & node) const override;
	const TCNodesVector & parents(const CBonusSystemNode & node) const override;
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override;
	const TNodesVector & children(const CBonusSystemNode & node) const override;
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override;

private:
	friend class PreparedBonusGraphFinalization;
	friend class battle::PreparedStackStateTransfer;
	class View final : public ProjectedBonusView
	{
		ProjectedBonusGraph & graph;
		const CBonusSystemNode & node;
	public:
		View(ProjectedBonusGraph & graph, const CBonusSystemNode & node) : graph(graph), node(node) {}
		const battle::Unit * evaluatedUnit() const override;
		std::vector<Bonus> sourceLocalSpellEffects() const override;
		void onBattleFormChanged(const battle::CUnitState & unit) const override;
		TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr) const override;
		TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
		/// Witness only, NOT a projection epoch; isBonusCacheable() is false.
		int32_t getTreeVersion() const override;
	};
	struct Entry { Values original; Values current; int32_t version; };
	struct Binding
	{
		const battle::CUnitState * unit;
		const ProjectedBonusRoster * roster;
		const CCreature * formSource;
		bool occurrence = false;
		int32_t originalRevision = 0;
		int32_t projectedRevision = 0;
		const CCreature * originalFormSource = nullptr;
		bool originalWidth = false;
		CreatureID originalCreature;
		std::optional<const BattleInfo *> nativeBattle = std::nullopt;
	};
	std::map<const CBonusSystemNode *, Entry> entries;
	struct DurationStorage;
	// Old identities remain alive: an old opaque ADD/removal handle must resolve
	// to this branch's latest value, never to a sibling's copy or equal payload.
	std::map<std::shared_ptr<Bonus>, std::shared_ptr<Bonus>> durationAliases;
	std::shared_ptr<Bonus> currentDurationRecord(std::shared_ptr<Bonus> record) const;
	void replaceDurationAliases(const std::shared_ptr<Bonus> & before, const std::shared_ptr<Bonus> & after);
	void reduceDurationsUnchecked(const CBonusSystemNode & source, const CSelector & selector,
		std::vector<ProjectedDurationChange> & changes, TCNodes & active);
	std::map<const CStack *, Binding> bindings;
	std::map<const CStack *, std::vector<recipientBonusOverlay::Layer>> recipientOverlays;
	void assignRecipientOverlays(const CStack & original,
		std::vector<recipientBonusOverlay::Layer> layers, bool replacing);
	TConstBonusListPtr queryOriginal(const CBonusSystemNode & node, const CSelector & selector, bool unstacked);
	TConstBonusListPtr queryRecipientLayer(const CStack & original, size_t count,
		const CSelector & selector, bool unstacked);
	std::map<const CBonusSystemNode *, std::unique_ptr<View>> views;
	bool poisoned = false;
	bool finalized = false;
	bool unjournaledReplacement = false;
	ProjectedBonusInvalidations invalidations;
	void requireUsable() const;
	void bindImpl(const CStack & original, const battle::CUnitState & unit,
		const ProjectedBonusRoster & roster, bool occurrences);
	void removeCapturedExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus);
	void removeBonusesRecursiveUnchecked(const CBonusSystemNode & root, const CSelector & selector, TCNodes & active,
		std::vector<ProjectedExportRemoval> * receipts = nullptr);
	bool hasExactView(const CStack & original, const battle::CUnitState & unit) const noexcept;
	void validate() const;
	void synchronizeForms();
	void propagate(const CBonusSystemNode & destination, const CBonusSystemNode & source,
		const std::shared_ptr<Bonus> & bonus, bool remove);
	void sourceRelation(const CBonusSystemNode & node, const CBonusSystemNode & source, bool attach);
	void redDescendantRelation(const CBonusSystemNode & source, const CBonusSystemNode & destination, bool attach);
	void prepareRelation(const CBonusSystemNode & node, const CBonusSystemNode & parent);
};
