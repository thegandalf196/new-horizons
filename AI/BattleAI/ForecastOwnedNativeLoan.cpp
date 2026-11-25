/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "HypotheticBattle.h"
#include "HypotheticUnitEnvironment.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/bonuses/OwnedFrozenScoringSource.h"
#include <stdexcept>
const battle::OwnedUnitStateLoanSource * HypotheticUnitEnvironment::ownedUnitStateLoanSource() const noexcept
{
	// Ownership survives an explicitly native prospective replay; numerical
	// routing alone is not a capability. Ordinary native/reference paths opt out.
	return owner.compactForecast || owner.compactReadSession ? this : nullptr;
}
std::shared_ptr<battle::CUnitState> HypotheticUnitEnvironment::acquireOwnedState(const battle::CUnitState & source) const
{
	return forecast::NativeAdapter::acquireOwnedState(owner, source);
}
namespace forecast
{
std::shared_ptr<battle::CUnitState> NativeAdapter::acquireOwnedState(const HypotheticBattle & branch,
	const battle::CUnitState & source)
{
	const auto * owned = branch.compactForecast ? &*branch.compactForecast
		: branch.compactReadSession ? &*branch.compactReadSession : nullptr;
	if(!owned || !source.usesStateEnvironment(*branch.unitEnvironment())
		|| !owned->unit(source.unitId()) || branch.getCapturedMagicRules() != owned->source().magicOwner)
		throw std::invalid_argument("Missing owned native loan capability");
	const auto * registered = branch.battleGetUnitByID(source.unitId());
	if(registered != &source)
	{
		const auto * target = dynamic_cast<const StackWithBonuses *>(registered);
		const auto * prepared = dynamic_cast<const StackWithBonuses *>(&source);
		if(!target || !prepared || !target->preparedEffectSourceMatches(*prepared))
			throw std::invalid_argument("Foreign or stale derived native loan source");
	}
	std::vector<NativeReadOccurrence> occurrences;
	occurrences.reserve(owned->orderedIds().size());
	std::shared_ptr<const NativeReadEnvironment> previous;
	for(const auto id : owned->orderedIds())
	{
		const auto * value = owned->unit(id);
		const auto * definition = owned->definition(id);
		if(!value || !definition || !definition->readDescriptor || !owned->readView(id))
			throw std::invalid_argument("Incomplete owned native loan occurrence");
		if(!previous) previous = owned->readView(id)->environmentOwner();
		if(branch.compactForecast && id != source.unitId())
		{
			// The value model is authoritative between accepted operations.
			// Reuse every untouched peer's complete owning handles; only native
			// prospective replay (routing off) may be ahead of these receipts.
			if(!value->nativeValues || !value->currentModifiers || !value->queries)
				throw std::invalid_argument("Incomplete untouched owned loan peer");
			occurrences.push_back({definition->readDescriptor, prepareReadValues(branch, id),
				value->currentModifiers, value->queries});
			continue;
		}
		const auto * current = id == source.unitId() ? &source
			: dynamic_cast<const battle::CUnitState *>(branch.battleGetUnitByID(id));
		if(!current) throw std::invalid_argument("Missing registered native loan peer");
		auto program = value->currentModifiers;
		auto queries = value->queries;
		std::shared_ptr<const frozenScoring::OwnedSource> scoring;
		if(const auto * writable = dynamic_cast<const StackWithBonuses *>(current))
		{
			if(writable->damageScoringBonuses || writable->damageScoringUnstackedBonuses)
			{
				const bonusDependencies::QueryFrameBinding binding{current->unitId(), current->creatureId(), current->getCount(),
					current->unitOwner(), current->unitSide(), current->unitSlot(), current->isGhost(), current->summoned,
					current->natureSummoned, definition->readDescriptor->creatures};
				scoring = writable->captureFrozenScoringSource(binding);
				if(!scoring) throw std::invalid_argument("Unowned frozen scoring loan source");
				program.reset(); queries.reset();
			}
			else std::tie(program, queries) = writable->ownedModifierFacts();
		}
		else if(const auto proof = NativeReadView::querySource(*current); proof && proof->scoring)
		{
			if(proof->descriptor->identity != definition->readDescriptor->identity
				|| proof->descriptor->creatures != definition->readDescriptor->creatures)
				throw std::invalid_argument("Foreign frozen scoring descriptor");
			scoring = proof->scoring; program.reset(); queries.reset();
		}
		if(!scoring && (!program || !queries || !program->ownsQueryFrame(*queries)))
			throw std::invalid_argument("Unbound native loan peer queries");
		// Complete value capture only. The raw programme/aliases are SHARED;
		// there is no collector, source-parent walk or formula reinterpretation.
		auto values = std::make_shared<const battle::UnitStateValueSnapshot>(battle::UnitStateValueSnapshot::capture(*current));
		occurrences.push_back({definition->readDescriptor, std::move(values), std::move(program), std::move(queries), std::move(scoring)});
	}
	auto environment = captureReadEnvironment(branch, std::move(previous));
	if(registered != &source)
	{
		// A permitted unpublished scratch replaces this exact occurrence.
		// Reprice its native membership using that scratch, not the old target.
		auto scratchEnvironment = std::make_shared<NativeReadEnvironment>(*environment);
		scratchEnvironment->warfare[source.unitId()] = branch.unitWarfareStatBonus(&source);
		environment = std::move(scratchEnvironment);
	}
	auto acquired = NativeReadView::acquireFromOwned(std::move(occurrences), environment, source.unitId(), branch.forecastWorkSink());
	if(!acquired.unit || !acquired.owner) throw std::logic_error("Empty owned native loan");
	return std::move(acquired.unit);
}
}
