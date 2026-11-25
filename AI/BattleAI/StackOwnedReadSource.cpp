/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackOwnedReadSource.h"
#include "StackWithBonuses.h"
#include "OwnedStackQuerySource.h"
#include "ForecastNativeReadView.h"
#include "HypotheticBattle.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include <cassert>
#include <limits>
#include <stdexcept>
std::unique_ptr<PreparedOwnedReadSource> StackWithBonuses::prepareOwnedReadSource(
	std::shared_ptr<const forecast::NativeReadView> source,
	std::shared_ptr<const bonusDependencies::CapturedModifiers> program, std::shared_ptr<const forecastEffects::State> effects) const
{
	if(!source || !program || !effects || ownedQuerySource || damageScoringBonuses || damageScoringUnstackedBonuses
		|| treeVersionLocal == std::numeric_limits<int>::max())
		throw std::invalid_argument("Incomplete owned native source preparation");
	auto prepared = std::make_unique<PreparedOwnedReadSource>();
	prepared->source = std::move(source);
	prepared->effects = std::move(effects);
	prepared->program = std::move(program);
	prepared->queries = prepared->source->queryFrame();
	if(!prepared->queries || !prepared->program->ownsQueryFrame(*prepared->queries)
		|| !prepared->queries->matches(id, getHealth().getCount(), creatureId()))
		throw std::invalid_argument("Foreign owned query source");
	prepared->sourceLease = prepared->program->bindRecipientLayerSource(prepared->queries);
	if(!prepared->sourceLease) throw std::invalid_argument("Unbound fixed recipient source lease");
	prepared->unit = &prepared->source->unit();
	prepared->type = prepared->unit->unitType();
	if(prepared->unit->unitId() != id || prepared->unit->unitOwner() != player
		|| prepared->unit->unitSide() != side || prepared->unit->unitSlot() != slot
		|| prepared->unit->unitBaseAmount() != static_cast<int32_t>(baseAmount))
		throw std::invalid_argument("Owned source identity mismatch");
	const auto spells = prepared->queries->sourceLocalSpellEffects();
	prepared->spells.emplace(spells.begin(), spells.end());
	return prepared;
}
void StackWithBonuses::publishOwnedReadSource(PreparedOwnedReadSource & prepared) noexcept
{
	assert(prepared.source && prepared.program && prepared.queries && prepared.unit);
	ownedReadSource.swap(prepared.source);
	modifierBaseEffects.swap(prepared.effects);
	modifierBaseProgram = prepared.program;
	modifierSourceLease.swap(prepared.sourceLease);
	modifierCurrentProgram.swap(prepared.program);
	modifierQueryFrame.swap(prepared.queries);
	exactLocalSpellEffects.swap(prepared.spells);
	originalUnit = prepared.unit;
	type = prepared.type;
	origBearer = modifierQueryFrame.get();
	// The immutable programme already owns the COMPLETE former leaf. A new
	// native write view starts with an empty leaf; never merge it twice.
	bonusesToAdd.clear(); bonusesToUpdate.clear(); bonusesToRemove.clear();
	projectedEffects.reset(); projectedUnstackedEffects.reset();
	capturedPhysicalAfflictionGroups.clear();
	projectedBearer.reset(); ownedBearer.reset();
	++treeVersionLocal;
	modifierQueriesVersion = treeVersionLocal;
}
namespace
{
const battle::CUnitState & checkedReadSource(const HypotheticBattle * owner,
	const std::shared_ptr<const forecast::NativeReadView> & source,
	const std::shared_ptr<const bonusDependencies::CapturedModifiers> & program)
{
	if(!owner || !source || !program || !source->queryFrame()
		|| !program->ownsQueryFrame(*source->queryFrame()))
		throw std::invalid_argument("Unbound owned native write source");
	return source->unit();
}
}
StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner,
	std::shared_ptr<const forecast::NativeReadView> source,
	std::shared_ptr<const bonusDependencies::CapturedModifiers> program, std::shared_ptr<const forecastEffects::State> effects)
	: battle::CUnitState(), treeVersionLocal(0), origBearer(nullptr), owner(Owner),
	  type(checkedReadSource(Owner, source, program).unitType()), sourceCreatureType(source->unit().creatureId()),
	  baseAmount(source->unit().unitBaseAmount()),
	  battleStartMaximumAggregateHP(source->unit().getBattleStartMaximumAggregateHP()),
	  id(source->unit().unitId()), side(source->unit().unitSide()), player(source->unit().unitOwner()), slot(source->unit().unitSlot())
{
	// Construction stays unpublished. Restore the SAME complete native value
	// inventory into this receiver's health/ammo/proxies; no form callback.
	if(!effects) throw std::invalid_argument("Owned native source has no effect ownership");
	modifierBaseEffects = std::move(effects);
	modifierBaseProgram = program;
	modifierCurrentProgram = std::move(program);
	modifierQueryFrame = source->queryFrame();
	modifierSourceLease = modifierBaseProgram->bindRecipientLayerSource(modifierQueryFrame);
	if(!modifierSourceLease) throw std::invalid_argument("Unbound owned write source lease");
	modifierQueriesVersion = treeVersionLocal;
	ownedReadSource = std::move(source);
	originalUnit = &ownedReadSource->unit();
	origBearer = modifierQueryFrame.get();
	const auto spells = modifierQueryFrame->sourceLocalSpellEffects();
	exactLocalSpellEffects.emplace(spells.begin(), spells.end());
	bindStateEnvironment(Owner->unitEnvironment());
	ownedReadSource->valuesOwner()->restore(*this);
}
