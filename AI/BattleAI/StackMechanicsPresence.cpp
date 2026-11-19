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
bool includeRecords(battle::MechanicsPresence & result, const Records & records,
	size_t & remaining, bool & cacheable)
{
	if(records.size() > remaining) return false;
	remaining -= records.size();
	for(const auto & record : records)
	{
		if constexpr(std::is_same_v<std::decay_t<decltype(record)>, Bonus>)
		{
			result.include(record);
			cacheable &= !record.updater && !record.propagationUpdater;
		}
		else
		{
			if(!record) return false;
			result.include(*record);
			cacheable &= !record->updater && !record->propagationUpdater;
		}
	}
	return true;
}
}

void StackWithBonuses::invalidateProjectionPresence() noexcept
{
	++projectionVersion;
	projectionPresence.reset();
}

void StackWithBonuses::exposeProjectionAliases(bool unstacked) const noexcept
{
	const bool pointers = damageScoringBonuses
		? unstacked && damageScoringUnstackedBonuses && !damageScoringUnstackedBonuses->empty()
		: !bonusesToAdd.empty() || !bonusesToUpdate.empty()
			|| (unstacked && projectedUnstackedEffects && !projectedUnstackedEffects->empty());
	if(!pointers)
		return;
	projectionAliasesEscaped = true;
	if(projectionAliasLease)
		*projectionAliasLease = true;
	projectionPresence.reset();
}

const StackWithBonuses::LocalPresence & StackWithBonuses::localProjectionPresence() const
{
	const bool aliases = projectionAliasesEscaped || (projectionAliasLease && *projectionAliasLease);
	if(projectionPresence && projectionPresence->version == projectionVersion
		&& projectionPresence->cacheable && !aliases && !projectionMutationDepth)
		return *projectionPresence;
	LocalPresence next;
	next.version = projectionVersion;
	size_t remaining = 8192;
	if(damageScoringBonuses)
		next.complete = includeRecords(next.types, *damageScoringBonuses, remaining, next.cacheable)
			&& (!damageScoringUnstackedBonuses
				|| includeRecords(next.types, *damageScoringUnstackedBonuses, remaining, next.cacheable));
	else
		next.complete = includeRecords(next.types, bonusesToAdd, remaining, next.cacheable)
			&& includeRecords(next.types, bonusesToUpdate, remaining, next.cacheable)
			&& (!projectedEffects || includeRecords(next.types, *projectedEffects, remaining, next.cacheable))
			&& (!projectedUnstackedEffects || includeRecords(next.types, *projectedUnstackedEffects, remaining, next.cacheable));
	next.records = 8192 - remaining;
	projectionPresence = std::move(next);
	return *projectionPresence;
}

bool StackWithBonuses::mayHaveBonusType(BonusType type) const
{
	const StackWithBonuses * layer = this;
	size_t remaining = 8192;
	for(size_t depth = 0; layer && depth < 512; ++depth)
	{
		if(typeid(*layer) != typeid(StackWithBonuses)) return true;
		if(type == BonusType::MORALE && layer->veteranCohesionEarned) return true;
		if(layer->damageScoringBonuses)
		{
			// This route is explicitly frozen: mergeBonusesImpl never consults
			// original providers or local overlays, even after movement/form edits.
			const auto & local = layer->localProjectionPresence();
			if(!local.complete || local.records > remaining) return true;
			return local.types.mayHave(type);
		}
		if(layer->ownedQuerySource || layer->hasBattleFormState()
			|| layer->damageScoringUnstackedBonuses
			|| !ProjectedAdjacencyBonuses::canProveTypeAbsence(*layer)) return true;
		const auto & local = layer->localProjectionPresence();
		if(!local.complete || local.records > remaining) return true;
		remaining -= local.records;
		if(local.types.mayHave(type)) return true;
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
