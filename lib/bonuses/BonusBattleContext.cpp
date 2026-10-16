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
	return battle::acceptsBattleBonus(dynamic_cast<const CStack &>(original), *state, bonus);
}
