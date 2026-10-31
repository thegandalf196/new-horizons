/*
 * BonusBattleContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BonusEvaluationContext.h"
#include "ProjectedBonusRoster.h"
#include "CBonusSystemNode.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../battle/BattleInfo.h"
#include "../battle/BattleBonusAcceptance.h"
#include "../mapObjects/army/CStackInstance.h"
#include <vcmi/Entity.h>
#include <stdexcept>

BonusEvaluationContext::BonusEvaluationContext(const CStack & node, const battle::Unit & unit,
	const ProjectedBonusRoster & values)
	: BonusEvaluationContext(node, unit)
{
	if(!values.contains(unit))
		throw std::invalid_argument("Projected bonus subject is outside its roster");
	roster = &values;
}

const battle::Unit * BonusEvaluationContext::battleUnit() const
{
	if(getNodeType() != BonusNodeType::STACK_BATTLE)
		return nullptr;
	return projected ? projected : dynamic_cast<const CStack *>(&original);
}

const CStackInstance * BonusEvaluationContext::stackInstance() const
{
	switch(getNodeType())
	{
	case BonusNodeType::STACK_INSTANCE:
		return dynamic_cast<const CStackInstance *>(&original);
	case BonusNodeType::STACK_BATTLE:
		return dynamic_cast<const CStack &>(original).base;
	default:
		return nullptr;
	}
}

const CCreature * BonusEvaluationContext::creature() const
{
	if(getNodeType() == BonusNodeType::CREATURE)
		return dynamic_cast<const CCreature *>(&original);
	if(const auto * unit = battleUnit())
		return unit->unitType();
	if(const auto * stack = stackInstance())
		return stack->getCreature();
	return nullptr;
}

std::optional<FactionID> BonusEvaluationContext::faction() const
{
	if(projected)
		return projected->unitType()->getFactionID();
	if(const auto * bearer = dynamic_cast<const INativeTerrainProvider *>(&original))
		return bearer->getFactionID();
	return std::nullopt;
}

std::vector<const battle::Unit *> BonusEvaluationContext::adjacentUnits() const
{
	if(projected)
	{
		if(!roster)
			throw std::logic_error("Projected adjacency requires its complete roster");
		if(native && !native->battle)
			throw std::logic_error("Explicitly unbound projected unit has no battle adjacency domain");
		return roster->adjacentUnits(*projected);
	}
	const auto & stack = dynamic_cast<const CStack &>(original);
	const auto units = stack.getBattle()->battleAdjacentUnits(&stack);
	return {units.begin(), units.end()};
}

bool BonusEvaluationContext::acceptsBonus(const Bonus & bonus) const
{
	if(!projected)
		return original.acceptsBonus(bonus);
	const auto * state = dynamic_cast<const battle::CUnitState *>(projected);
	if(!state)
		throw std::logic_error("Projected acceptance requires a CUnitState view");
	if(native)
		return battle::acceptsBattleBonus(dynamic_cast<const CStack &>(original), *state, bonus, native->hero);
	return battle::acceptsBattleBonus(dynamic_cast<const CStack &>(original), *state, bonus);
}

BonusEvaluationContext::BonusEvaluationContext(const CStack & node, const battle::Unit & unit,
	const ProjectedBonusRoster & roster, NativeContext provenance)
	: BonusEvaluationContext(node, unit, roster)
{
	native = provenance;
}

bool BonusEvaluationContext::hasNativeContext() const noexcept { return native.has_value(); }

const BattleInfo * BonusEvaluationContext::battleContext() const
{
	if(native)
		return native->battle;
	const auto * stack = dynamic_cast<const CStack *>(&original);
	return stack ? stack->getBattle() : nullptr;
}

std::optional<TerrainId> BonusEvaluationContext::battleTerrain() const
{
	const auto * battle = battleContext();
	return battle ? std::optional<TerrainId>(battle->getTerrainType()) : std::nullopt;
}

std::optional<TerrainId> BonusEvaluationContext::queryTerrain() const
{
	if(native)
		return native->queryTerrain; // explicit missing provenance never falls back
	const auto * stack = dynamic_cast<const CStack *>(&original);
	return stack ? stack->getQueryTerrain() : std::nullopt;
}


bool AdjacentUnitFilter::isDefault() const
{
	return relationship == Relationship::ANY && nativeTrait == NativeTrait::ANY && !includeUpgrades && !anyCreature;
}
bool AdjacentUnitFilter::isValid() const
{
	return relationship <= Relationship::OPPOSING_ARMY && nativeTrait <= NativeTrait::LIVING;
}
bool BonusEvaluationContext::matchesAdjacentUnit(const battle::Unit & neighbor, CreatureID family,
	const AdjacentUnitFilter & filter) const
{
	if(filter.isDefault())
		return neighbor.creatureId() == family; // exact historical default, including its eligibility
	const auto * subject = battleUnit();
	if(!subject || !neighbor.alive() || neighbor.isGhost() || neighbor.unitId() == subject->unitId())
		return false;
	const bool sameArmy = neighbor.unitOwner() == subject->unitOwner();
	if((filter.relationship == AdjacentUnitFilter::Relationship::SAME_ARMY && !sameArmy)
		|| (filter.relationship == AdjacentUnitFilter::Relationship::OPPOSING_ARMY && sameArmy))
		return false;
	const auto * creature = neighbor.unitType();
	if(!creature)
		return false;
	if((filter.nativeTrait == AdjacentUnitFilter::NativeTrait::UNDEAD && !creature->hasBonusOfType(BonusType::UNDEAD))
		|| (filter.nativeTrait == AdjacentUnitFilter::NativeTrait::LIVING && !creature->isLiving()))
		return false;
	if(filter.anyCreature || neighbor.creatureId() == family)
		return true;
	if(!filter.includeUpgrades || !family.hasValue())
		return false;
	const auto * base = family.toCreature();
	if(!base)
		throw std::runtime_error("Adjacency filter references a missing creature family");
	return base->isMyDirectOrIndirectUpgrade(creature);
}
