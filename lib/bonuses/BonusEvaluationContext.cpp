/*
 * BonusEvaluationContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BonusEvaluationContext.h"
#include "CBonusSystemNode.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../battle/Unit.h"
#include "../mapObjects/army/CStackInstance.h"
#include <stdexcept>

BonusEvaluationContext::BonusEvaluationContext(const CBonusSystemNode & node) noexcept
	: original(node)
{
}

BonusEvaluationContext::BonusEvaluationContext(const CStack & node, const battle::Unit & unit)
	: original(node), projected(&unit)
{
	if(node.unitId() != unit.unitId())
		throw std::invalid_argument("Projected bonus unit does not match its original node");
}

const CBonusSystemNode & BonusEvaluationContext::originalNode() const noexcept
{
	return original;
}

BonusNodeType BonusEvaluationContext::getNodeType() const
{
	return original.getNodeType();
}

PlayerColor BonusEvaluationContext::getOwner() const
{
	return projected ? projected->unitOwner() : original.getOwner();
}

std::optional<int> BonusEvaluationContext::stackCount() const
{
	if(getNodeType() == BonusNodeType::STACK_INSTANCE || getNodeType() == BonusNodeType::COMMANDER)
		return dynamic_cast<const CStackInstance &>(original).getCount();
	if(getNodeType() == BonusNodeType::STACK_BATTLE)
	{
		const auto & stack = dynamic_cast<const CStack &>(original);
		return projected ? projected->getCount() : stack.getCount();
	}
	return std::nullopt;
}

std::optional<int> BonusEvaluationContext::stackLevel() const
{
	if(getNodeType() == BonusNodeType::STACK_INSTANCE || getNodeType() == BonusNodeType::COMMANDER)
		return dynamic_cast<const CStackInstance &>(original).getLevel();
	if(getNodeType() == BonusNodeType::STACK_BATTLE)
	{
		const auto & stack = dynamic_cast<const CStack &>(original);
		if(stack.base == nullptr)
			return (projected ? projected->unitType() : stack.unitType())->getLevel();
		return dynamic_cast<const CStackInstance *>(stack.base)->getLevel();
	}
	return std::nullopt;
}
