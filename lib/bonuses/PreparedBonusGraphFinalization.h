/*
 * PreparedBonusGraphFinalization.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "PreparedBonusGraphStorage.h"

class ProjectedBonusGraph;

/// Nonrouting final storage transfer for the existing projected operations.
/// Requires explicit mutable authority for EVERY captured node, including
/// capacity-only reservation of unchanged nodes. Never casts away constness.
/// Caller retains all original nodes/shared Bonus pointees and exclusive,
/// nonreentrant graph access from capture through commit/destruction. This API
/// creates no lock/world lease and does not enforce external readers' lifetime.
/// Preparation can allocate, including capacity-only live list reservations;
/// failure leaves logical graph/cache/counter data unchanged, capacity may grow.
/// Success consumes the projected query owner; no further query/edit is legal.
/// Freshness checks precede the first live write. After reservation, commit is
/// bounded precomputed-row transfer: no graph walk, callback, selector or log.
/// Not a whole-world transaction or permission to publish an UPDATE packet.
class DLL_LINKAGE PreparedBonusGraphFinalization final : boost::noncopyable
{
public:
	PreparedBonusGraphFinalization(ProjectedBonusGraph & graph, const TNodesVector & mutableNodes);
	bool readyToCommit() const noexcept;
	bool commit() noexcept;
private:
	PreparedBonusGraphStorage storage;
};
