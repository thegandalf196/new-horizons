/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "CGarrisonInt.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/CCreatureHandler.h"

namespace
{
bool companyArmy(const CArmedInstance * army)
{
	const auto * mine = dynamic_cast<const CGMine *>(army);
	return mine && mine->getCompanyOwner().isValidPlayer() && mine->getCompanyOwner() == mine->getOwner();
}

bool companyRogueAggregate(const CGarrisonSlot & selected)
{
	const auto * mine = dynamic_cast<const CGMine *>(selected.getObj());
	return mine && mine->hasCompanyDefenders() && mine->hasStackAtSlot(selected.getSlot())
		&& mine->getStack(selected.getSlot()).getCreatureID() == CreatureID(CreatureID::decode("core:rogue"));
}
}

void CGarrisonInt::bulkMoveArmy(const CGarrisonSlot * selected)
{
	if(!checkSelected(selected))
		return;

	const auto srcArmyType = selected->upg;
	const auto destArmyType = (srcArmyType == EGarrisonType::UPPER)
		? EGarrisonType::LOWER
		: EGarrisonType::UPPER;

	if(showStackTransferError(selected))
		return;

	auto srcArmy = armedObjs[srcArmyType];
	auto destArmy = armedObjs[destArmyType];

	if(!destArmy)
		return;
	if(companyArmy(srcArmy) || companyArmy(destArmy))
	{
		GAME->interface()->showInfoDialog("Mercenary Company defenders remain at their mine. Move other guards individually.");
		return;
	}

	const auto srcSlot = selected->ID;
	GAME->interface()->cb->bulkMoveArmy(srcArmy->id, destArmy->id, srcSlot);
}

void CGarrisonInt::bulkMergeStacks(const CGarrisonSlot * selected)
{
	if(!checkSelected(selected))
		return;

	if(companyRogueAggregate(*selected))
	{
		GAME->interface()->showInfoDialog("Mercenary Company defenders cannot be merged or rebalanced. Move ordinary Rogues individually.");
		return;
	}
	const auto type = selected->upg;

	if(!armedObjs[type]->hasCreatureSlots(selected->creature, selected->ID))
		return;

	GAME->interface()->cb->bulkMergeStacks(armedObjs[type]->id, selected->ID);
}

void CGarrisonInt::bulkSplitStack(const CGarrisonSlot * selected)
{
	if(!checkSelected(selected, 1)) // check if > 1
		return;

	const auto type = selected->upg;

	if(getEmptySlot(type, selected->creature->getId()) == SlotID())
		return;

	GAME->interface()->cb->bulkSplitStack(armedObjs[type]->id, selected->ID);
}

void CGarrisonInt::bulkSplitAndRebalanceStack(const CGarrisonSlot * selected)
{
	if(!checkSelected(selected, 1))
		return;

	if(companyRogueAggregate(*selected))
	{
		GAME->interface()->showInfoDialog("Mercenary Company defenders cannot be merged or rebalanced. Move ordinary Rogues individually.");
		return;
	}
	const auto type = selected->upg;

	// Do not disturb the server if the creature is already balanced
	if(getEmptySlot(type, selected->creature->getId()) == SlotID() && armedObjs[type]->isCreatureBalanced(selected->creature))
		return;

	GAME->interface()->cb->bulkSplitAndRebalanceStack(armedObjs[type]->id, selected->ID);
}
