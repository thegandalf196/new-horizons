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

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
bool checkLeadershipSwap(const CArmedInstance * leftArmy, const CArmedInstance * rightArmy,
	SlotID leftSlot, SlotID rightSlot)
{
	const auto * leftCreature = leftArmy ? leftArmy->getCreature(leftSlot) : nullptr;
	const auto * rightCreature = rightArmy ? rightArmy->getCreature(rightSlot) : nullptr;
	if(!leftCreature || !rightCreature)
		return UIHelper::checkLeadershipTransfer(leftArmy, rightArmy, leftSlot, rightSlot,
			leftCreature ? leftArmy->getStackCount(leftSlot) : 0)
			&& UIHelper::checkLeadershipTransfer(rightArmy, leftArmy, rightSlot, leftSlot,
				rightCreature ? rightArmy->getStackCount(rightSlot) : 0);

	// A swap replaces each stack; equal types are merged through the normal
	// garrison path instead and are not sent here.
	return UIHelper::checkLeadershipResult(rightArmy, leftCreature->getId(), leftArmy->getStackCount(leftSlot))
		&& UIHelper::checkLeadershipResult(leftArmy, rightCreature->getId(), rightArmy->getStackCount(rightSlot));
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
			if(!checkLeadershipSwap(left, right, slotID, slotID))
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

		if(!checkLeadershipSwap(left, right, leftIt->first, rightIt->first))
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
			if(!UIHelper::checkLeadershipTransfer(left, right, leftIt->first, leftIt->first,
				left->getStackCount(leftIt->first)))
				return;
			GAME->interface()->cb->swapCreatures(left, right, leftIt->first, leftIt->first);
		}

	for(; rightIt != rightSlots.end(); rightIt++)
		if (!left->hasStackAtSlot(rightIt->first))
		{
			if(!UIHelper::checkLeadershipTransfer(right, left, rightIt->first, rightIt->first,
				right->getStackCount(rightIt->first)))
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

	SlotID targetSlot = target->getSlotFor(creature);
	if(targetSlot.validSlot())
	{
		if(source->stacksCount() == 1 && source->needsLastStack())
		{
			if(!UIHelper::checkLeadershipTransfer(source, target, sourceSlot, targetSlot,
				source->getStackCount(sourceSlot) - 1))
				return;
			GAME->interface()->cb->splitStack(source, target, sourceSlot, targetSlot,
				target->getStackCount(targetSlot) + source->getStackCount(sourceSlot) - 1);
		}
		else
		{
			if(!UIHelper::checkLeadershipTransfer(source, target, sourceSlot, targetSlot,
				source->getStackCount(sourceSlot)))
				return;
			GAME->interface()->cb->mergeOrSwapStacks(source, target, sourceSlot, targetSlot);
		}
	}
}

void CExchangeController::moveSingleStackCreature(bool leftToRight, SlotID sourceSlot, bool forceEmptySlotTarget)
{
	const auto source = leftToRight ? left : right;
	const auto target = leftToRight ? right : left;
	auto creature = source->getCreature(sourceSlot);

	if(creature == nullptr || source->stacksCount() == 1)
		return;

	SlotID targetSlot = forceEmptySlotTarget ? target->getFreeSlot() : target->getSlotFor(creature);
	if(targetSlot.validSlot())
	{
		if(!UIHelper::checkLeadershipTransfer(source, target, sourceSlot, targetSlot, 1))
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
