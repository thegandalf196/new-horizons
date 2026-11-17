/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "ForecastAcceptedSnapshotSeal.h"
#include "AttackPossibility.h"
#include "VisibleCandidateWorld.h"
#include "HeroActionAdmission.h"
#include "HypotheticBattle.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include <stdexcept>
namespace forecast
{
namespace
{
void require(bool valid)
{
	if(!valid) throw std::logic_error("Accepted snapshot has foreign or unowned source provenance");
}
const CStack * nativeRoot(const battle::Unit & unit)
{
	if(const auto * native = dynamic_cast<const CStack *>(&unit)) return native;
	const auto * projected = dynamic_cast<const StackWithBonuses *>(&unit);
	std::vector<recipientBonusOverlay::Layer> layers;
	const CStack * native = nullptr;
	return projected && projected->captureRecipientOverlayLayers(layers, native) ? native : nullptr;
}
bool sameIdentity(const battle::CUnitState & unit, const NativeReadDescriptor & descriptor)
{
	const auto & identity = descriptor.identity;
	return unit.unitId() == identity.id && unit.unitBaseAmount() == identity.baseAmount
		&& unit.battleFormOriginalCreature() == identity.originalCreature
		&& unit.unitSide() == identity.side && unit.unitOwner() == identity.owner && unit.unitSlot() == identity.slot;
}
}
void NativeAdapter::validateAcceptedSnapshot(const battle::CUnitState & snapshot,
	const AttackPossibility & action)
{
	require(action.snapshotSeal && action.snapshotSeal->matches(action) && action.snapshotSeal->contains(snapshot)
		&& action.queryWorld && action.queryWorld->callback());
	const auto & query = *action.queryWorld->callback();
	const auto * effects = action.effectPreview.get();
	const auto * source = query.forecastState();
	require(source && action.forecastSource && action.forecastSource->sameSnapshot(*source));
	const auto * original = query.battleGetUnitByID(snapshot.unitId());
	const auto * definition = source ? source->definition(snapshot.unitId()) : nullptr;
	require(original && definition && definition->readDescriptor);
	const auto & descriptor = *definition->readDescriptor;
	require(sameIdentity(snapshot, descriptor));
	const auto ownedSnapshot = NativeReadView::querySource(snapshot);
	if(ownedSnapshot) require(!ownedSnapshot->scoring);
	if(effects)
	{
		require(effects->battleGetUnitByID(snapshot.unitId()) == &snapshot);
		heroActionProjection::CanonicalSourceAdmission proof(snapshot, *effects);
		require(proof.admits(snapshot));
		if(const auto * writable = dynamic_cast<const StackWithBonuses *>(&snapshot))
		{
			const auto [program, frame] = writable->ownedModifierFacts();
			if(program && frame && program->ownsQueryFrame(*frame)
				&& frame->binding().profileOwner == descriptor.creatures) return;
		}
		if(ownedSnapshot && ownedSnapshot->descriptor->identity == descriptor.identity
			&& ownedSnapshot->descriptor->creatures == descriptor.creatures) return;
		const auto * root = nativeRoot(snapshot);
		require(root && root == nativeRoot(*original)); // Null==null is NEVER source proof.
		return;
	}
	if(ownedSnapshot)
	{
		std::shared_ptr<const bonusDependencies::CapturedModifiers> expected;
		if(const auto * writable = dynamic_cast<const StackWithBonuses *>(original))
			expected = writable->ownedModifierFacts().first;
		else if(const auto * state = dynamic_cast<const battle::CUnitState *>(original))
			if(const auto owned = NativeReadView::querySource(*state)) expected = owned->modifiers;
		require(expected && ownedSnapshot->modifiers == expected && ownedSnapshot->queries
			&& expected->ownsQueryFrame(*ownedSnapshot->queries)
			&& ownedSnapshot->descriptor->identity == descriptor.identity
			&& ownedSnapshot->descriptor->creatures == descriptor.creatures);
		return; // The same programme may own a different Count-derived frame.
	}
	const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(&snapshot);
	require(nativeRoot(*original) && detached && detached->hasUnitInfoAncestor(*original)
		&& detached->hasBonusProviderAncestor(*original));
}
}
