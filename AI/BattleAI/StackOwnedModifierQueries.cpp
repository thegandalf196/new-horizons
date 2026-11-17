/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include <stdexcept>

recipientBonusOverlay::Layer StackWithBonuses::ownModifierLayer() const
{
	return recipientBonusOverlay::Layer({bonusesToAdd, bonusesToUpdate, bonusesToRemove,
		projectedEffects, projectedUnstackedEffects, capturedPhysicalAfflictionGroups, veteranCohesionEarned});
}
std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> StackWithBonuses::currentModifierQueries() const
{
	if(!modifierBaseProgram || !modifierCurrentProgram || !modifierQueryFrame || !modifierSourceLease || !exactLocalSpellEffects)
		throw std::logic_error("Incomplete owned modifier query binding");
	auto binding = modifierQueryFrame->binding();
	if(binding.recipientId != unitId() || binding.creature != creatureId()
		|| binding.owner != unitOwner() || binding.side != unitSide() || binding.slot != unitSlot()
		|| binding.ghost != ghost || binding.summoned != summoned || binding.natureSummoned != natureSummoned)
		throw std::logic_error("Owned native modifier profile changed without preparation");
	const bool countChanged = binding.count != health.getCount();
	const bool leafChanged = modifierQueriesVersion != treeVersionLocal;
	if(!countChanged && !leafChanged) return modifierQueryFrame;
	binding.count = health.getCount();
	auto program = modifierCurrentProgram;
	if(leafChanged)
	{
		program = modifierBaseProgram->appendRecipientLayer(ownModifierLayer(),
			*exactLocalSpellEffects, *modifierQueryFrame, modifierSourceLease.get());
		if(!program) throw std::logic_error("Unsupported owned native recipient operation");
	}
	// Same native recipe kernel as outer compact publication. Native replay
	// and acquireState may query changed count BEFORE the outer receipt exists.
	// This is a branch-private cache at a real change, never a provider walk or
	// search-budget debit in a numeric getter.
	auto frame = program->refreshQueries(binding, countChanged ? bonusDependencies::Change::Count
		: bonusDependencies::Change::None, leafChanged ? nullptr : modifierQueryFrame);
	if(!frame) throw std::logic_error("Unsupported owned native query dependency change");
	modifierCurrentProgram = std::move(program);
	modifierQueryFrame = std::move(frame);
	modifierQueriesVersion = treeVersionLocal;
	return modifierQueryFrame;
}

std::pair<std::shared_ptr<const bonusDependencies::CapturedModifiers>,
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>> StackWithBonuses::ownedModifierFacts() const
{
	if(damageScoringBonuses || damageScoringUnstackedBonuses)
		throw std::logic_error("Frozen scoring requires its explicit owning answer source");
	const auto current = currentModifierQueries();
	return {modifierCurrentProgram, current};
}
