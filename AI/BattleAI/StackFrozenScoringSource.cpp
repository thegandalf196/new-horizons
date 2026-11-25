/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/OwnedFrozenScoringSource.h"
std::shared_ptr<const frozenScoring::OwnedSource> StackWithBonuses::captureFrozenScoringSource(
	bonusDependencies::QueryFrameBinding binding) const
{
	if(typeid(*this) != typeid(StackWithBonuses) || !damageScoringBonuses
		|| binding.recipientId != unitId() || binding.creature != creatureId() || binding.count != getHealth().getCount()
		|| binding.owner != unitOwner() || binding.side != unitSide() || binding.slot != unitSlot()
		|| binding.ghost != isGhost() || binding.summoned != summoned || binding.natureSummoned != natureSummoned) return {};
	// Already materialized frozen answers only. No source graph/provider/callback
	// is captured or imported; this grants no originating raw/canonical rights.
	return frozenScoring::OwnedSource::capture(std::move(binding), *damageScoringBonuses,
		damageScoringUnstackedBonuses, veteranCohesionEarned);
}
