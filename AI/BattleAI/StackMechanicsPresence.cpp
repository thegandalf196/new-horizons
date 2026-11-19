/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "ProjectedAdjacencyBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/MechanicsPresence.h"
#include <type_traits>

namespace
{
template<typename Records>
bool includeRecords(battle::MechanicsPresence & result, const Records & records, size_t & remaining)
{
	if(records.size() > remaining) return false;
	remaining -= records.size();
	for(const auto & record : records)
	{
		if constexpr(std::is_same_v<std::decay_t<decltype(record)>, Bonus>)
			result.include(record);
		else
		{
			if(!record) return false;
			result.include(*record);
		}
	}
	return true;
}
}

bool StackWithBonuses::mayHaveBonusType(BonusType type) const
{
	const StackWithBonuses * layer = this;
	size_t remaining = 8192;
	for(size_t depth = 0; layer && depth < 512; ++depth)
	{
		if(typeid(*layer) != typeid(StackWithBonuses)) return true;
		if(type == BonusType::MORALE && layer->veteranCohesionEarned) return true;
		battle::MechanicsPresence local;
		if(layer->damageScoringBonuses)
		{
			// This route is explicitly frozen: mergeBonusesImpl never consults
			// original providers or local overlays, even after movement/form edits.
			if(!includeRecords(local, *layer->damageScoringBonuses, remaining)
				|| (layer->damageScoringUnstackedBonuses
					&& !includeRecords(local, *layer->damageScoringUnstackedBonuses, remaining))) return true;
			return local.mayHave(type);
		}
		if(layer->ownedQuerySource || layer->hasBattleFormState()
			|| layer->damageScoringUnstackedBonuses
			|| !ProjectedAdjacencyBonuses::canProveTypeAbsence(*layer)) return true;
		// These containers are intentionally public and mutable. Read their
		// current values, including edits without a version bump; no stale mirror.
		if(!includeRecords(local, layer->bonusesToAdd, remaining)
			|| !includeRecords(local, layer->bonusesToUpdate, remaining)
			|| (layer->projectedEffects && !includeRecords(local, *layer->projectedEffects, remaining))
			|| (layer->projectedUnstackedEffects && !includeRecords(local, *layer->projectedUnstackedEffects, remaining))) return true;
		if(local.mayHave(type)) return true;
		// Removals only subtract possibilities. Continue through the original
		// source even after a selected snapshot: inactive inherited auras matter.
		if(!layer->originalUnit || layer->originalUnit->unitId() != id
			|| layer->originalUnit->getBonusBearer() != layer->origBearer) return true;
		if(const auto * native = dynamic_cast<const CStack *>(layer->origBearer))
			return native->mayHaveBonusType(type);
		layer = dynamic_cast<const StackWithBonuses *>(layer->origBearer);
		if(!layer) return true;
	}
	return true; // Cyclic/deep or foreign provenance is unknown, never absence.
}
