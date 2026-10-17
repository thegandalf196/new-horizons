/*
 * StackMeleeBonusSource.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "ProjectedMeleeExpiry.h"
#include "../../lib/CStack.h"
#include <type_traits>

namespace
{
bool meleeExpiry(const Bonus * bonus)
{
	return bonus && (Bonus::UntilAttack(bonus) || Bonus::UntilOwnAttack(bonus)
		|| Bonus::UntilBeingAttacked(bonus) || Bonus::untilAfterAttackSequence(bonus));
}

template<typename Records> bool ambiguousCopies(const Records & records)
{
	return std::ranges::any_of(records, [](const auto & record)
	{
		if constexpr(std::is_same_v<std::decay_t<decltype(record)>, Bonus>)
			return meleeExpiry(&record) || record.propagator;
		else
			return !record || meleeExpiry(record.get()) || record->propagator;
	});
}
}

bool StackWithBonuses::captureMeleeBonusSource(MeleeBonusSource & result) const
{
	result = {};
	std::set<const StackWithBonuses *> seen;
	std::set<std::shared_ptr<Bonus>> excluded;
	const IBonusBearer * source = this;
	size_t remaining = 8192;
	while(const auto * layer = dynamic_cast<const StackWithBonuses *>(source))
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || seen.size() >= 512 || !seen.insert(layer).second
			|| !layer->originalUnit || layer->originalUnit->unitId() != id || layer->hasBattleFormState()
			|| layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses)
			return false;
		// These legacy value snapshots have discarded export-owner identity.
		// A copied propagated record has no recoverable export/delta provenance,
		// regardless of duration. Native source exports are still admitted below.
		if((layer->projectedEffects && ambiguousCopies(*layer->projectedEffects))
			|| (layer->projectedUnstackedEffects && ambiguousCopies(*layer->projectedUnstackedEffects))
			|| (layer->exactLocalSpellEffects && ambiguousCopies(*layer->exactLocalSpellEffects)))
			return false;
		for(const auto & removed : layer->bonusesToRemove)
		{
			if(!removed || remaining == 0 || removed->propagator)
				return false; // Prior local-only aura removal has unknown global provenance.
			--remaining;
			excluded.insert(removed);
		}
		for(const auto * records : {&layer->bonusesToAdd, &layer->bonusesToUpdate})
			for(const auto & record : *records)
			{
				if(!record || remaining == 0 || record->propagator)
					return false;
				--remaining;
				if(meleeExpiry(record.get()) && !excluded.contains(record))
				{
					result.localExports.push_back(record);
				}
			}
		source = layer->origBearer;
	}
	result.native = dynamic_cast<const CStack *>(source);
	return result.native && typeid(*result.native) == typeid(CStack) && result.native->unitId() == id;
}

void StackWithBonuses::suppressMeleeBonusIdentities(const std::set<std::shared_ptr<Bonus>> & records)
{
	bonusesToRemove.insert(records.begin(), records.end());
	const auto selected = [&records](const auto & record) { return records.contains(record); };
	vstd::erase_if(bonusesToAdd, selected);
	vstd::erase_if(bonusesToUpdate, selected);
	++treeVersionLocal;
}
