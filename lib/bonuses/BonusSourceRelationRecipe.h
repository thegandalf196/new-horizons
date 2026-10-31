/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusGraphQuery.h"

/// One source relation's exports, in the native order. The immediate attached
/// source remains the evaluation source even for records exported by ancestors.
/// Readers/visitors own their graph lease; this does not capture nodes or edges.
template<typename ExportReader, typename Visitor>
void forEachPropagatingSourceRecord(const BonusGraphQuery & graph, const CBonusSystemNode & source,
	ExportReader && exports, Visitor && visit)
{
	for(const auto & bonus : exports(source))
		if(bonus->propagator)
			visit(bonus);
	TCNodes ancestors;
	bonusRedAncestors(graph, source, ancestors);
	for(const auto * ancestor : ancestors)
		for(const auto & bonus : exports(*ancestor))
			if(bonus->propagator)
				visit(bonus);
}

/// Only the destination's accepted list is edited. No recursion, native graph
/// mutation, invalidation, timer update, source resolution or ownership capture.
DLL_LINKAGE std::shared_ptr<Bonus> appendPropagatedArrival(BonusList & accepted,
	const std::shared_ptr<Bonus> & bonus, const CBonusSystemNode & destination,
	const BonusEvaluationContext & source);
struct PropagatedArrivalRemoval
{
	bool accepted;
	bool removed;
};
/// accepted retains the legacy invalidation decision even if no record existed.
/// Removal never evaluates the source or invokes a propagation updater.
DLL_LINKAGE PropagatedArrivalRemoval removePropagatedArrival(BonusList & accepted,
	const std::shared_ptr<Bonus> & bonus, const CBonusSystemNode & destination);
