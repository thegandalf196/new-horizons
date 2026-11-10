/*
 * CExchangeController.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CExchangeController.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../UIHelper.h"

#include "CGarrisonInt.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
bool checkCommandSwap(const CArmedInstance * leftArmy, const CArmedInstance * rightArmy,
	SlotID leftSlot, SlotID rightSlot)
{
	const auto * leftCreature = leftArmy ? leftArmy->getCreature(leftSlot) : nullptr;
	const auto * rightCreature = rightArmy ? rightArmy->getCreature(rightSlot) : nullptr;
	return (!leftCreature || UIHelper::checkArmySlot(rightArmy, rightSlot, leftCreature->getId()))
		&& (!rightCreature || UIHelper::checkArmySlot(leftArmy, leftSlot, rightCreature->getId()));
}
}

CExchangeController::CExchangeController(ObjectInstanceID hero1, ObjectInstanceID hero2)
	: left(GAME->interface()->cb->getHero(hero1))
	, right(GAME->interface()->cb->getHero(hero2))
{
}

void CExchangeController::swapArmy()
{
	const auto & leftSlots = left->Slots();
	const auto & rightSlots = right->Slots();

	auto leftIt = leftSlots.begin();
	auto rightIt = rightSlots.begin();

	// Swap slots that are full in both armies
	// [A] [B] => [B] [A]
	for (SlotID slotID(0); slotID < GameConstants::ARMY_SIZE; ++slotID)
	{
		if (left->hasStackAtSlot(slotID) && right->hasStackAtSlot(slotID))
		{
			if(!checkCommandSwap(left, right, slotID, slotID))
				return;
			GAME->interface()->cb->swapCreatures(left, right, slotID, slotID);
		}
	}

	// Swap pairs of stacks in different slots and correct their positions
	// [A] [ ]    [B] [ ]    [ ] [A]
	//         =>         =>
	// [ ] [B]    [ ] [A]    [B] [ ]
	for (;;)
	{
		while (leftIt != leftSlots.end() && right->hasStackAtSlot(leftIt->first))
			leftIt++;

		while (rightIt != rightSlots.end() && left->hasStackAtSlot(rightIt->first))
			rightIt++;

		if (leftIt == leftSlots.end() || rightIt == rightSlots.end())
			break;

		if(!checkCommandSwap(left, right, leftIt->first, rightIt->first)
			|| !UIHelper::checkArmySlot(left, rightIt->first, rightIt->second->getCreatureID())
			|| !UIHelper::checkArmySlot(right, leftIt->first, leftIt->second->getCreatureID()))
			return;
		GAME->interface()->cb->swapCreatures(left, right, leftIt->first, rightIt->first);

		GAME->interface()->cb->swapCreatures(left, left, leftIt->first, rightIt->first);
		GAME->interface()->cb->swapCreatures(right, right, rightIt->first, leftIt->first);

		leftIt++;
		rightIt++;
	}

	// Move remaining unpaired stacks (if armies size is different)
	// [A] [ ] => [ ] [A]
	for(; leftIt != leftSlots.end(); leftIt++)
		if (!right->hasStackAtSlot(leftIt->first))
		{
			if(!UIHelper::checkArmySlot(right, leftIt->first, leftIt->second->getCreatureID()))
				return;
			GAME->interface()->cb->swapCreatures(left, right, leftIt->first, leftIt->first);
		}

	for(; rightIt != rightSlots.end(); rightIt++)
		if (!left->hasStackAtSlot(rightIt->first))
		{
			if(!UIHelper::checkArmySlot(left, rightIt->first, rightIt->second->getCreatureID()))
				return;
			GAME->interface()->cb->swapCreatures(left, right, rightIt->first, rightIt->first);
		}
}

void CExchangeController::moveArmy(bool leftToRight, std::optional<SlotID> heldSlot)
{
	const auto source = leftToRight ? left : right;
	const auto target = leftToRight ? right : left;

	if(!heldSlot.has_value())
	{
		const auto & weakestSlot = vstd::minElementByFun(source->Slots(),
			[](const auto & s) -> int
			{
				return s.second->getCreatureID().toCreature()->getAIValue();
			});
		heldSlot = weakestSlot->first;
	}
	
	if (source->getCreature(heldSlot.value()) == nullptr)
		return;

	GAME->interface()->cb->bulkMoveArmy(source->id, target->id, heldSlot.value());
}

void CExchangeController::moveStack(bool leftToRight, SlotID sourceSlot)
{
	const auto source = leftToRight ? left : right;
	const auto target = leftToRight ? right : left;
	auto creature = source->getCreature(sourceSlot);

	if(creature == nullptr)
		return;

	SlotID targetSlot = newHorizonsHeroes::recruitmentSlot(target, creature->getId());
	if(!targetSlot.validSlot())
		return;

	const bool mustKeepLastSourceCreature = source->stacksCount() == 1 && source->needsLastStack();
	const TQuantity sourceCount = source->getStackCount(sourceSlot);
	if(mustKeepLastSourceCreature && sourceCount <= 1)
	{
		GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("core.tcommand.5"));
		return;
	}

	if(target->getCreature(targetSlot))
	{
		GAME->interface()->cb->mergeStacks(source, target, sourceSlot, targetSlot);
		return;
	}

	if(mustKeepLastSourceCreature)
	{
		// Whole-stack intent retains the source's required last creature.
		GAME->interface()->cb->mergeOrSwapStacks(source, target, sourceSlot, targetSlot);
		return;
	}

	const TQuantity amountToMove = sourceCount;
	if(amountToMove <= 0)
		return;
	if(!UIHelper::checkArmySlot(target, targetSlot, creature->getId()))
		return;

	GAME->interface()->cb->mergeOrSwapStacks(source, target, sourceSlot, targetSlot);
}

void CExchangeController::moveSingleStackCreature(bool leftToRight, SlotID sourceSlot, bool forceEmptySlotTarget)
{
	const auto source = leftToRight ? left : right;
	const auto target = leftToRight ? right : left;
	auto creature = source->getCreature(sourceSlot);

	if(creature == nullptr || source->stacksCount() == 1)
		return;

	SlotID targetSlot = newHorizonsHeroes::recruitmentSlot(target, creature->getId());
	if(forceEmptySlotTarget)
	{
		targetSlot = SlotID();
		for(const auto empty : target->getFreeSlots())
			if(UIHelper::armySlotProblem(target, empty, creature->getId()).empty())
			{
				targetSlot = empty;
				break;
			}
	}
	if(targetSlot.validSlot())
	{
		if(!UIHelper::checkArmySlot(target, targetSlot, creature->getId()))
			return;
		GAME->interface()->cb->splitStack(source, target, sourceSlot, targetSlot, target->getStackCount(targetSlot) + 1);
	}
}

void CExchangeController::swapArtifacts(bool equipped, bool baclpack)
{
	GAME->interface()->cb->bulkMoveArtifacts(left->id, right->id, true, equipped, baclpack);
}

void CExchangeController::moveArtifacts(bool leftToRight, bool equipped, bool baclpack)
{
	const auto source = leftToRight ? left : right;
	const auto target = leftToRight ? right : left;

	GAME->interface()->cb->bulkMoveArtifacts(source->id, target->id, false, equipped, baclpack);
}
