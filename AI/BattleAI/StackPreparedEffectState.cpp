/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include <cassert>
#include <limits>
#include <stdexcept>
#include <type_traits>

std::unique_ptr<StackWithBonuses> StackWithBonuses::prepareEffectDraft(const HypotheticBattle * prospective) const
{
	if(typeid(*this) != typeid(StackWithBonuses) || ownedQuerySource || !originalUnit || hasBattleFormState() || damageScoringBonuses
		|| damageScoringUnstackedBonuses || treeVersionLocal > std::numeric_limits<int>::max() - 16384)
		throw std::invalid_argument("Unsupported prepared effect source");
	// Construct against exactly the original source, NOT against this layered
	// view. Current overlays below are copied ONCE; no inherited double bonus.
	if(prospective && (prospective->getBattleID() != owner->getBattleID()
		|| prospective->getCapturedMagicRules() != owner->getCapturedMagicRules()))
		throw std::invalid_argument("Foreign prospective effect context");
	auto prepared = ownedReadSource
		? std::make_unique<StackWithBonuses>(prospective ? prospective : owner, ownedReadSource,
			modifierBaseProgram, modifierBaseEffects)
		: std::make_unique<StackWithBonuses>(prospective ? prospective : owner, originalUnit);
	battle::UnitStateValueSnapshot::capture(*this).restore(*prepared);
	prepared->origBearer = origBearer;
	prepared->originalUnit = originalUnit;
	prepared->projectedBearer = projectedBearer;
	prepared->ownedBearer = ownedBearer;
	prepared->ownedReadSource = ownedReadSource;
	prepared->modifierBaseProgram = modifierBaseProgram;
	prepared->modifierSourceLease = modifierSourceLease;
	prepared->modifierCurrentProgram = modifierCurrentProgram;
	prepared->modifierQueryFrame = modifierQueryFrame;
	prepared->modifierBaseEffects = modifierBaseEffects;
	prepared->bonusesToAdd = bonusesToAdd;
	prepared->bonusesToUpdate = bonusesToUpdate;
	prepared->bonusesToRemove = bonusesToRemove;
	prepared->projectedEffects = projectedEffects;
	prepared->projectedUnstackedEffects = projectedUnstackedEffects;
	prepared->capturedPhysicalAfflictionGroups = capturedPhysicalAfflictionGroups;
	prepared->exactLocalSpellEffects = exactLocalSpellEffects;
	prepared->treeVersionLocal = treeVersionLocal + 1;
	prepared->modifierQueriesVersion = prepared->treeVersionLocal;
	prepared->preparedEffectBaseVersion = treeVersionLocal;
	prepared->preparedEffectOriginOwner = owner;
	return prepared;
}
bool StackWithBonuses::preparedEffectSourceMatches(const StackWithBonuses & prepared) const noexcept
{
	return !ownedQuerySource && !prepared.ownedQuerySource && owner == prepared.preparedEffectOriginOwner
		&& origBearer == prepared.origBearer && originalUnit == prepared.originalUnit
		&& projectedBearer == prepared.projectedBearer && ownedBearer == prepared.ownedBearer
		&& ownedReadSource == prepared.ownedReadSource && modifierBaseProgram == prepared.modifierBaseProgram
		&& modifierSourceLease == prepared.modifierSourceLease
		&& id == prepared.id && type == prepared.type && sourceCreatureType == prepared.sourceCreatureType
		&& side == prepared.side && player == prepared.player && slot == prepared.slot
		&& baseAmount == prepared.baseAmount && battleStartMaximumAggregateHP == prepared.battleStartMaximumAggregateHP
		&& !damageScoringBonuses && !damageScoringUnstackedBonuses
		&& !prepared.damageScoringBonuses && !prepared.damageScoringUnstackedBonuses
		&& prepared.preparedEffectBaseVersion == treeVersionLocal
		&& treeVersionLocal < std::numeric_limits<int>::max()
		&& usesStateEnvironment(*owner->unitEnvironment())
		&& prepared.usesStateEnvironment(*prepared.owner->unitEnvironment());
}
void StackWithBonuses::publishPreparedEffects(StackWithBonuses & prepared) noexcept
{
	assert(preparedEffectSourceMatches(prepared));
	static_assert(std::is_nothrow_swappable_v<decltype(projectedEffects)>);
	static_assert(std::is_nothrow_swappable_v<decltype(projectedUnstackedEffects)>);
	static_assert(std::is_nothrow_swappable_v<decltype(exactLocalSpellEffects)>);
	static_assert(noexcept(capturedPhysicalAfflictionGroups.swap(prepared.capturedPhysicalAfflictionGroups)));
	bonusesToAdd.swap(prepared.bonusesToAdd);
	bonusesToUpdate.swap(prepared.bonusesToUpdate);
	bonusesToRemove.swap(prepared.bonusesToRemove);
	projectedEffects.swap(prepared.projectedEffects);
	projectedUnstackedEffects.swap(prepared.projectedUnstackedEffects);
	capturedPhysicalAfflictionGroups.swap(prepared.capturedPhysicalAfflictionGroups);
	exactLocalSpellEffects.swap(prepared.exactLocalSpellEffects);
	modifierCurrentProgram.swap(prepared.modifierCurrentProgram);
	modifierQueryFrame.swap(prepared.modifierQueryFrame);
	commitPreparedFrozenApplication(prepared);
	// Existing this-bound caches stay in place and observe a monotonic version.
	++treeVersionLocal;
	modifierQueriesVersion = treeVersionLocal;
}
