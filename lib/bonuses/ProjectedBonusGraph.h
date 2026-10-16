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
#include <map>

class ProjectedBonusRoster;
class JsonNode;
namespace battle { class CUnitState; class CUnitStateDetached; }

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
	const ProjectedBonusView & view(const CBonusSystemNode & node);
	/// Private staging edits preserve legacy exported/materialized propagation.
	/// Failure poisons this private owner; originals stay unchanged. Discard it.
	void addExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus);
	void removeExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus);
	/// Preserve authored sequential order after no-hook value edits. Queries
	/// auto-synchronize a sole pending form, never guess several edits' order.
	void synchronizeForm(const CStack & original);
	/// Call only after world-level UPDATE ignore/occupancy checks. On load
	/// failure discard this owner AND its possibly partially loaded private unit.
	void loadBoundState(const CStack & original, battle::CUnitStateDetached & state, const JsonNode & data);
	TConstBonusListPtr query(const CBonusSystemNode & node, const CSelector & selector, bool unstacked);

	const BonusList & local(const CBonusSystemNode & node) const override;
	const TCNodesVector & parents(const CBonusSystemNode & node) const override;
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override;
	const TNodesVector & children(const CBonusSystemNode & node) const override;
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override;

private:
	friend class PreparedBonusGraphFinalization;
	class View final : public ProjectedBonusView
	{
		ProjectedBonusGraph & graph;
		const CBonusSystemNode & node;
	public:
		View(ProjectedBonusGraph & graph, const CBonusSystemNode & node) : graph(graph), node(node) {}
		const battle::Unit * evaluatedUnit() const override;
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
	};
	std::map<const CBonusSystemNode *, Entry> entries;
	std::map<const CStack *, Binding> bindings;
	std::map<const CBonusSystemNode *, std::unique_ptr<View>> views;
	bool poisoned = false;
	bool finalized = false;
	bool unjournaledReplacement = false;
	ProjectedBonusInvalidations invalidations;
	void requireUsable() const;
	bool hasExactView(const CStack & original, const battle::CUnitState & unit) const noexcept;
	void validate() const;
	void synchronizeForms();
	void propagate(const CBonusSystemNode & destination, const CBonusSystemNode & source,
		const std::shared_ptr<Bonus> & bonus, bool remove);
	void sourceRelation(const CStack & node, const CCreature & source, bool attach);
};
