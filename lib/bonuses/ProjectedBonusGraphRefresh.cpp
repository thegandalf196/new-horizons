/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "../battle/StackEffectRefresh.h"
#include <stdexcept>

void ProjectedBonusGraph::refreshExported(const CBonusSystemNode & source, const Bonus & value)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		const auto selected = values(source).exported; // Exact raw exporting source, not effective bonuses.
		for(const auto & record : selected)
		{
			const auto before = currentDurationRecord(record);
			if(!stackEffectRefresh::matchesIdentity(value, before.get())) continue;
			auto after = std::make_shared<Bonus>(*before);
			stackEffectRefresh::refreshFields(*after, value);
			replaceDurationAliases(before, after); // One alias pool, including other local/export lists.
		}
		// Native ordinary UPDATE invalidates once even when no local record matches.
		invalidations.record(true, &source, *this);
	}
	catch(...) { poisoned = true; throw; }
}
