/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusEvaluationContext.h"
#include "../battle/Unit.h"
#include "../CCreatureHandler.h"
#include <stdexcept>
namespace
{
struct NativeType
{
	const CCreature * value;
	explicit operator bool() const { return value != nullptr; }
	bool undead() const { return value->hasBonusOfType(BonusType::UNDEAD); }
	bool living() const { return value->isLiving(); }
	bool upgrade(CreatureID family) const
	{
		const auto * base = family.toCreature();
		if(!base) throw std::runtime_error("Adjacency filter references a missing creature family");
		return base->isMyDirectOrIndirectUpgrade(value);
	}
};
struct NativeNeighbor
{
	const battle::Unit & value;
	CreatureID creature() const { return value.creatureId(); }
	bool alive() const { return value.alive(); }
	bool ghost() const { return value.isGhost(); }
	uint32_t id() const { return value.unitId(); }
	PlayerColor owner() const { return value.unitOwner(); }
	NativeType type() const { return {value.unitType()}; }
};
struct CopiedType
{
	const OwnedBonusEvaluationFacts::AdjacentCreature * value;
	explicit operator bool() const { return value != nullptr; }
	bool undead() const { return value->undead; }
	bool living() const { return value->living; }
	bool upgrade(CreatureID family) const
	{
		const auto found = value->familyUpgrades.find(family);
		if(found == value->familyUpgrades.end()) throw std::logic_error("Owned adjacency family facts are unknown");
		return found->second;
	}
};
struct CopiedNeighbor
{
	const OwnedBonusEvaluationFacts::AdjacentUnit & value;
	const OwnedBonusEvaluationFacts & facts;
	CreatureID creature() const { return value.creature; }
	bool alive() const { return value.alive; }
	bool ghost() const { return value.ghost; }
	uint32_t id() const { return value.id; }
	PlayerColor owner() const { return value.owner; }
	CopiedType type() const
	{
		if(!facts.adjacencyTraits) throw std::logic_error("Owned adjacency creature traits are unknown");
		const auto found = facts.adjacencyTraits->find(value.creature);
		if(found == facts.adjacencyTraits->end()) throw std::logic_error("Owned adjacency creature traits are unknown");
		return {found->second ? &*found->second : nullptr};
	}
};
template<class Neighbor>
bool matches(const BonusEvaluationContext & context, const Neighbor & neighbor, CreatureID family,
	const AdjacentUnitFilter & filter)
{
	if(filter.isDefault())
		return neighbor.creature() == family;
	const auto * subject = context.battleUnit();
	if(!subject || !neighbor.alive() || neighbor.ghost() || neighbor.id() == subject->unitId())
		return false;
	const bool sameArmy = neighbor.owner() == subject->unitOwner();
	if((filter.relationship == AdjacentUnitFilter::Relationship::SAME_ARMY && !sameArmy)
		|| (filter.relationship == AdjacentUnitFilter::Relationship::OPPOSING_ARMY && sameArmy))
		return false;
	const auto creature = neighbor.type();
	if(!creature)
		return false;
	if((filter.nativeTrait == AdjacentUnitFilter::NativeTrait::UNDEAD && !creature.undead())
		|| (filter.nativeTrait == AdjacentUnitFilter::NativeTrait::LIVING && !creature.living()))
		return false;
	if(filter.anyCreature || neighbor.creature() == family)
		return true;
	if(!filter.includeUpgrades || !family.hasValue())
		return false;
	return creature.upgrade(family);
}
}
bool BonusEvaluationContext::matchesAdjacentUnit(const battle::Unit & neighbor, CreatureID family,
	const AdjacentUnitFilter & filter) const
{
	if(owned) throw std::logic_error("Owned adjacency cannot borrow a native neighbor");
	return matches(*this, NativeNeighbor{neighbor}, family, filter);
}
bool BonusEvaluationContext::hasMatchingAdjacentUnit(CreatureID family, const AdjacentUnitFilter & filter) const
{
	if(owned)
	{
		if(!owned->adjacencyUnits) throw std::logic_error("Owned visibility-authorized adjacency is unknown");
		for(const auto & unit : *owned->adjacencyUnits)
			if(matches(*this, CopiedNeighbor{unit, *owned}, family, filter)) return true;
		return false;
	}
	for(const auto * unit : adjacentUnits())
		if(matchesAdjacentUnit(*unit, family, filter)) return true;
	return false;
}
