/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../lib/battle/UnitCreatureResolver.h"
#include "../../lib/battle/CommittedElementalEffects.h"
#include "../../lib/CStack.h"

void HypotheticBattle::captureElementalAttunement(const Subject & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get()))
	{
		elementalAttunementProfiles = parent->elementalAttunementProfiles;
		elementalAttunementMasks = parent->elementalAttunementMasks;
		elementalAttunementProfileKnown = parent->elementalAttunementProfileKnown;
		committedElementalFacts = parent->committedElementalFacts;
		committedElementalEffects = parent->committedElementalEffects;
		return;
	}
	const battle::LiveUnitEnvironmentContext context(*source);
	committedElementalEffects = context.committedElementalEffects();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const bool known = context.knowsElementalAttunementProfile(side);
		elementalAttunementProfileKnown.at(side) = known;
		elementalAttunementProfiles.at(side) = known
			? source->getBattle()->getElementalAttunementProfile(side)
			: newHorizonsElementalAttunement::Profile{};
		elementalAttunementMasks.at(side) = source->getBattle()->getElementalAttunementMask(side);
	}
	if(!committedElementalEffects) return;
	// Ghosts and turrets remain registered native occurrences in the complete
	// current-bonus roster; they must not be mistaken for projected creations.
	for(const auto * unit : source->battleGetUnitsIf([](const battle::Unit *) { return true; }))
	{
		const auto * native = dynamic_cast<const CStack *>(unit);
		if(!native || source->battleGetUnitByID(unit->unitId()) != unit)
			throw std::invalid_argument("Elemental effects require the registered native roster");
		committedElementalFacts.emplace(unit->unitId(), CommittedElementalFacts{
			std::shared_ptr<const battle::Unit>(source, unit), unit->getBonusBearer(),
			unit->creatureId(), native->battleFormOriginalCreature(), source->battleGetOwner(unit),
			native->getBattleFormViewRevision()});
	}
}

bool HypotheticBattle::admitsCommittedElementalFacts(const battle::Unit & unit,
	const CommittedElementalFacts & facts, unsigned depth) const
{
	if(depth >= 512) return false;
	const auto * state = dynamic_cast<const battle::CUnitState *>(&unit);
	if(!state || state->battleFormOriginalCreature() != facts.originalCreature) return false;
	// The retained native occurrence remains frozen. Descendants may use the
	// engine's typed form and controller transitions without acquiring a Hero policy.
	if(&unit == facts.source.get())
		return unit.getBonusBearer() == facts.provider && unit.creatureId() == facts.creature
			&& battleGetOwner(&unit) == facts.controller
			&& state->getBattleFormViewRevision() == facts.formRevision;
	if(const auto * projected = dynamic_cast<const StackWithBonuses *>(&unit))
		return projected->originalUnit && projected->origBearer
			&& projected->originalUnit->getBonusBearer() == projected->origBearer
			&& admitsCommittedElementalFacts(*projected->originalUnit, facts, depth + 1);
	if(const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(&unit))
	{
		// Detached ancestry stops at a projected wrapper. Resolve that exact
		// shared info/provider endpoint before continuing its native lineage.
		const battle::Unit * source = nullptr;
		try { source = dynamic_cast<const battle::Unit *>(detached->sourceLocalEffectProvider()); }
		catch(const std::runtime_error &) { return false; } // Mismatched info/provider provenance.
		return source && source->unitId() == unit.unitId()
			&& detached->hasUnitInfoAncestor(*source)
			&& detached->hasBonusProviderAncestor(*source)
			&& admitsCommittedElementalFacts(*source, facts, depth + 1);
	}
	return false;
}

void HypotheticBattle::setElementalAttunementMask(BattleSide side, uint8_t mask)
{
	if((mask & ~newHorizonsElementalAttunement::ALL_ELEMENTS) != 0)
		throw std::invalid_argument("Unknown projected elemental Attunement flag");
	auto & current = elementalAttunementMasks.at(side);
	if(current == mask)
		return;
	if(!elementalAttunementProfileKnown.at(side))
		throw std::invalid_argument("An unknown hero profile cannot project a new elemental activation");
	current = mask;
	++bonusTreeVersion;
	// An untouched recipient may still be the source's native/parent unit. Bind
	// matching creatures to this branch before its unit-level damage queries.
	const auto units = battleGetUnitsIf([&](const battle::Unit * unit)
	{
		if(!unit || unit->unitSide() != side) return false;
		if(const auto * resolver = unit->unitCreatureResolver())
			return newHorizonsElementalAttunement::passivesForCreatureKey(
				newHorizonsElementalAttunement::Profile{.rank = 1},
				newHorizonsElementalAttunement::ALL_ELEMENTS,
				resolver->encode(unit->creatureId().getNum())).attuned;
		return newHorizonsElementalAttunement::eligibleCreature(unit->creatureId());
	});
	for(const auto * unit : units)
		getForUpdate(unit->unitId());
}

newHorizonsElementalAttunement::UnitPassives HypotheticBattle::unitElementalAttunement(
	const battle::Unit * unit) const
{
	if(!unit)
		return {};
	const auto side = unit->unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	if(!elementalAttunementProfileKnown.at(side))
	{
		// Same-owner unpublished clones are used by atomic effect exchange and
		// damage snapshots, including localInit before map publication. They still
		// require the exact retained source/provider proof below; an ID is insufficient.
		if(const auto * projected = dynamic_cast<const StackWithBonuses *>(unit))
			if(projected->owner != this && battleGetUnitByID(unit->unitId()) != unit)
				throw std::invalid_argument("Projected elemental facts require a branch-bound unit");
		if(const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(unit))
			if(!detached->usesStateEnvironment(*unitEnvironment()))
				throw std::invalid_argument("Detached elemental facts require the branch environment");
		const auto found = committedElementalFacts.find(unit->unitId());
		if(!committedElementalEffects || found == committedElementalFacts.end()
			|| !admitsCommittedElementalFacts(*unit, found->second))
			throw std::invalid_argument("Projected elemental facts require their admitted occurrence, form and controller");
		return committedElementalEffects->evaluate(*unit);
	}
	if(const auto * resolver = unit->unitCreatureResolver())
		return newHorizonsElementalAttunement::passivesForCreatureKey(elementalAttunementProfiles.at(side),
			elementalAttunementMasks.at(side), resolver->encode(unit->creatureId().getNum()));
	return newHorizonsElementalAttunement::passivesForCreature(elementalAttunementProfiles.at(side),
		elementalAttunementMasks.at(side), unit->creatureId());
}
