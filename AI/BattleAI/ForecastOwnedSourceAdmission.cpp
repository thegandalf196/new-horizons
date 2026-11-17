/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "HypotheticBattle.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/bonuses/RecipientLayerSource.h"
#include <typeinfo>
namespace forecast
{
bool NativeAdapter::admitsOwnedPair(const HypotheticBattle & branch,
	const battle::Unit & actor, const battle::Unit & target)
{
	const auto * owned = branch.compactForecast ? &*branch.compactForecast
		: branch.compactReadSession ? &*branch.compactReadSession : nullptr;
	if(typeid(branch) != typeid(HypotheticBattle) || !owned
		|| branch.getCapturedMagicRules() != owned->source().magicOwner) return false;
	for(const auto * unit : {&actor, &target})
	{
		if(branch.battleGetUnitByID(unit->unitId()) != unit) return false;
		const auto * definition = owned->definition(unit->unitId());
		const auto * value = owned->unit(unit->unitId());
		if(!definition || !definition->readDescriptor || !value || !owned->readView(unit->unitId())) return false;
		const auto & profile = definition->readDescriptor->creatures;
		if(const auto * writable = dynamic_cast<const StackWithBonuses *>(unit))
		{
			if(typeid(*writable) != typeid(StackWithBonuses) || writable->owner != &branch
				|| !writable->usesStateEnvironment(*branch.unitEnvironment()) || !writable->ownedReadSource
				|| writable->damageScoringBonuses || writable->damageScoringUnstackedBonuses
				|| !writable->modifierBaseProgram || !writable->modifierSourceLease
				|| !writable->modifierBaseProgram->ownsRecipientLayerSource(*writable->modifierSourceLease)) return false;
			const auto [program, frame] = writable->ownedModifierFacts();
			if(!program || !frame || !program->ownsQueryFrame(*frame)
				|| frame->binding().profileOwner != profile
				|| !frame->matches(unit->unitId(), unit->getCount(), unit->creatureId())) return false;
		}
		else
		{
			// Exact registered immutable receiver, not an equal-ID foreign copy.
			if(unit != &owned->readView(unit->unitId())->unit() || !value->currentModifiers || !value->queries
				|| !value->currentModifiers->ownsQueryFrame(*value->queries)
				|| value->queries->binding().profileOwner != profile
				|| !value->queries->matches(unit->unitId(), unit->getCount(), unit->creatureId())) return false;
		}
	}
	return true;
}
}
