/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "HeroActionAdmission.h"
#include "ProjectedAdjacencyBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include <stdexcept>

namespace
{
class CurrentNativeModifierGraph final : public BonusGraphQuery
{
	const CStack & native;
	const StackWithBonuses & current;
public:
	CurrentNativeModifierGraph(const CStack & native, const StackWithBonuses & current)
		: native(native), current(current) {}
	const BonusList & local(const CBonusSystemNode & node) const override { return liveBonusGraph().local(node); }
	const TCNodesVector & parents(const CBonusSystemNode & node) const override { return liveBonusGraph().parents(node); }
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override { return liveBonusGraph().propagatingParents(node); }
	const TNodesVector & children(const CBonusSystemNode & node) const override { return liveBonusGraph().children(node); }
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override
	{
		return &node == &native ? BonusEvaluationContext(native, current) : liveBonusGraph().evaluation(node);
	}
};
}
std::shared_ptr<const bonusDependencies::CapturedModifiers> StackWithBonuses::capturePreparedModifierFacts(
	const StackWithBonuses & staged) const
{
	if(modifierBaseProgram || modifierCurrentProgram || modifierQueryFrame || modifierSourceLease
		|| staged.modifierBaseProgram || staged.modifierCurrentProgram || staged.modifierQueryFrame || staged.modifierSourceLease)
	{
		// Owned path after sealing. No ancestry walk, native collector, original
		// raw source or parent scalar read is permitted beyond this boundary.
		if(!modifierBaseProgram || !modifierCurrentProgram || !modifierQueryFrame || !modifierSourceLease
			|| !staged.modifierCurrentProgram || !staged.modifierQueryFrame || !owner || !staged.owner
			|| staged.modifierBaseProgram != modifierBaseProgram || staged.modifierSourceLease != modifierSourceLease
			|| !modifierBaseProgram->ownsRecipientLayerSource(*modifierSourceLease)
			|| !modifierCurrentProgram->ownsQueryFrame(*modifierQueryFrame)
			|| !staged.modifierCurrentProgram->ownsQueryFrame(*staged.modifierQueryFrame)
			|| !preparedEffectSourceMatches(staged) || !staged.exactLocalSpellEffects) return {};
		if(staged.owner != owner && staged.owner->battleGetUnitByID(id) != &staged) return {};
		const auto & binding = staged.modifierQueryFrame->binding();
		if(binding.recipientId != staged.unitId() || binding.creature != staged.creatureId()
			|| binding.owner != staged.unitOwner() || binding.side != staged.unitSide() || binding.slot != staged.unitSlot()
			|| binding.ghost != staged.ghost || binding.summoned != staged.summoned
			|| binding.natureSummoned != staged.natureSummoned) return {};
		const auto leaf = staged.ownModifierLayer();
		const auto result = modifierBaseProgram->appendRecipientLayer(leaf,
			*staged.exactLocalSpellEffects, *staged.modifierQueryFrame, modifierSourceLease.get());
		return preparedEffectSourceMatches(staged) && staged.modifierBaseProgram == modifierBaseProgram
			&& staged.modifierSourceLease == modifierSourceLease ? result : nullptr;
	}
	// Called under the enclosing CanonicalSourceAdmission interval. This seam
	// establishes same-source draft lineage/freshness, not new native rights.
	if(!preparedEffectSourceMatches(staged) || !owner || !staged.owner
		|| owner->initialExportSubject() || staged.owner->initialExportSubject()
		|| !hasCanonicalDamageQuerySources(*owner)
		|| !staged.hasCanonicalDamageQuerySources(*staged.owner)) return {};
	// Opaque committed export-operation programs and dynamic passive source
	// expansion require their own exact graph; never substitute live raw rows.
	if(!ProjectedAdjacencyBonuses::canProveTypeAbsence(*this)
		|| !ProjectedAdjacencyBonuses::canProveTypeAbsence(staged)
		|| ProjectedAdjacencyBonuses::captureSources(*staged.owner)) return {};
	if(staged.owner != owner && staged.owner->battleGetUnitByID(id) != &staged) return {};
	heroActionProjection::CanonicalSourceAdmission admission(*this, *owner);
	if(!admission.admits(*this)) return {};
	std::vector<recipientBonusOverlay::Layer> layers;
	const CStack * native = nullptr;
	if(!staged.captureRecipientOverlayLayers(layers, native) || !native
		|| native->hasBattleFormState() || native->veteranCohesionEarned) return {};
	const CurrentNativeModifierGraph graph(*native, staged);
	auto result = std::make_shared<const bonusDependencies::CapturedModifiers>(
		bonusDependencies::captureNativeOverlayModifiers(graph, *native, layers));
	// Receipt must still bind the target after all cold source/alias work.
	return preparedEffectSourceMatches(staged) ? result : nullptr;
}

int StackWithBonuses::preparedModifierWarfare() const
{
	if(!owner || !usesStateEnvironment(*owner->unitEnvironment()))
		throw std::invalid_argument("Unbound prepared modifier environment");
	return owner->unitWarfareStatBonus(this);
}
