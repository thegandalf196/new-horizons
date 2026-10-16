/*
 * ProjectedBonusInvalidations.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "CBonusSystemNode.h"
#include <map>
#include <optional>

class BonusGraphQuery;
class PreparedBonusGraphFinalization;

/// Preparation-only legacy generation/child-walk journal. No live writes.
/// The caller holds original node lifetimes and excludes graph access/reentry.
class DLL_LINKAGE ProjectedBonusInvalidations final
{
public:
	struct Event { bool advance; TCNodesVector targets; };
	void record(bool advance, const CBonusSystemNode * root, const BonusGraphQuery & graph);
	const std::vector<Event> & recordedEvents() const noexcept { return events; }
private:
	friend class PreparedBonusGraphFinalization;
	std::optional<int32_t> initialGeneration;
	int32_t generation = 0;
	struct Stamp { int32_t version; bool invalidated = false; };
	std::map<const CBonusSystemNode *, Stamp> stamps;
	std::vector<Event> events;
	bool borrowedCollision = false;
};
