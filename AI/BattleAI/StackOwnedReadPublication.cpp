/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
bool StackWithBonuses::stageOwnedModifierFacts(
	std::shared_ptr<const bonusDependencies::CapturedModifiers> program,
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queries)
{
	if(!modifierBaseProgram || !modifierSourceLease || !modifierQueryFrame || !program || !queries
		|| !modifierBaseProgram->ownsRecipientLayerSource(*modifierSourceLease)
		|| !program->ownsQueryFrame(*queries)) return false;
	const auto & fixed = modifierQueryFrame->binding();
	const auto & binding = queries->binding();
	if(binding.profileOwner != fixed.profileOwner || binding.recipientId != unitId()
		|| binding.count != health.getCount() || binding.creature != creatureId()
		|| binding.owner != unitOwner() || binding.side != unitSide() || binding.slot != unitSlot()
		|| binding.ghost != ghost || binding.summoned != summoned
		|| binding.natureSummoned != natureSummoned) return false;
	// Never swap fixed base, source lease, owner, environment, proxy or cache.
	modifierCurrentProgram = std::move(program);
	modifierQueryFrame = std::move(queries);
	modifierQueriesVersion = treeVersionLocal;
	return true;
}
