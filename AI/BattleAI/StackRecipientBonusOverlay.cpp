/*
 * StackRecipientBonusOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "../../lib/CStack.h"

recipientBonusOverlay::View StackWithBonuses::recipientOverlayView() const
{
	return {bonusesToAdd, bonusesToUpdate, bonusesToRemove, projectedEffects,
		projectedUnstackedEffects, capturedPhysicalAfflictionGroups, veteranCohesionEarned};
}

bool StackWithBonuses::captureRecipientOverlayLayers(std::vector<recipientBonusOverlay::Layer> & layers,
	const CStack * & native) const
{
	layers.clear();
	native = nullptr;
	std::set<const StackWithBonuses *> seen;
	const IBonusBearer * source = this;
	size_t remaining = 8192;
	while(const auto * layer = dynamic_cast<const StackWithBonuses *>(source))
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || seen.size() >= 512 || !seen.insert(layer).second
			|| !layer->originalUnit || layer->originalUnit->unitId() != id || layer->hasBattleFormState()
			|| layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses)
			return false;
		const auto view = layer->recipientOverlayView();
		for(const auto * records : {&view.added, &view.updated})
		{
			if(records->size() > remaining || std::ranges::any_of(*records, [](const auto & record) { return !record; }))
				return false;
			remaining -= records->size();
		}
		if(view.removed.size() > remaining || view.removed.contains(nullptr))
			return false;
		remaining -= view.removed.size();
		if(view.stackedSnapshot)
		{
			if(view.stackedSnapshot->size() > remaining)
				return false;
			remaining -= view.stackedSnapshot->size();
		}
		if(view.unstackedSnapshot)
		{
			if(view.unstackedSnapshot->size() > remaining
				|| std::ranges::any_of(*view.unstackedSnapshot, [](const auto & record) { return !record; }))
				return false;
			remaining -= view.unstackedSnapshot->size();
		}
		layers.emplace_back(view);
		source = layer->origBearer;
	}
	native = dynamic_cast<const CStack *>(source);
	if(!native || typeid(*native) != typeid(CStack) || native->unitId() != id)
		return false;
	std::ranges::reverse(layers); // Original native recipient outward through the real parent order.
	return true;
}
