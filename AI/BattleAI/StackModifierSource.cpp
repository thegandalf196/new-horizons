/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "ProjectedMeleeExpiry.h"
#include "ProjectedAdjacencyBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
std::shared_ptr<const bonusDependencies::CapturedModifiers> StackWithBonuses::captureModifierFacts(
	const battle::OwnedUnitBonusFrame * authorizedOwnedFrame) const
{
	if(authorizedOwnedFrame)
	{
		if(authorizedOwnedFrame->unit().unitId() != id) return {};
		return std::make_shared<const bonusDependencies::CapturedModifiers>(
			battle::OwnedFrameBranchQueries::captureModifiers(*authorizedOwnedFrame));
	}
	// A recorded program/overlay must use its authorized owned raw graph. The
	// plain native original is not a substitute for those recipient layers.
	if(owner->initialExportSubject() || ProjectedAdjacencyBonuses::captureSources(*owner)) return {};
	std::set<const StackWithBonuses *> seen;
	const IBonusBearer * source = this;
	while(const auto * layer = dynamic_cast<const StackWithBonuses *>(source))
	{
		if(typeid(*layer) != typeid(StackWithBonuses) || seen.size() >= 512
			|| !seen.insert(layer).second || layer->hasBattleFormState()
			|| layer->veteranCohesionEarned || layer->damageScoringBonuses || layer->damageScoringUnstackedBonuses)
			return {};
		if(!layer->bonusesToAdd.empty() || !layer->bonusesToUpdate.empty() || !layer->bonusesToRemove.empty()
			|| layer->projectedEffects || layer->projectedUnstackedEffects || layer->exactLocalSpellEffects)
		{
			// Same validated current source, not the stale native original.
			auto staged = prepareEffectDraft();
			return capturePreparedModifierFacts(*staged);
		}
		source = layer->origBearer;
	}
	MeleeBonusSource proof;
	if(!captureMeleeBonusSource(proof) || !proof.localExports.empty() || !proof.native
		|| proof.native->hasBattleFormState() || proof.native->veteranCohesionEarned
		|| isGhost() != proof.native->isGhost() || summoned != proof.native->summoned
		|| natureSummoned != proof.native->natureSummoned || unitOwner() != proof.native->unitOwner())
		return {};
	return std::make_shared<const bonusDependencies::CapturedModifiers>(
		bonusDependencies::captureNativeModifiers(liveBonusGraph(), *proof.native));
}
