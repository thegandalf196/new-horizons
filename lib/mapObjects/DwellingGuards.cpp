/*
 * DwellingGuards.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGDwelling.h"
#include "NewHorizonsNeutralArmies.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../networkPacks/PacksForClient.h"

void CGDwelling::updateGuards(IGameEventCallback & gameEvents) const
{
	//TODO: store custom guard config and use it
	//TODO: store boolean flag for guards

	bool guarded = false;
	//default condition - creatures are of level 5 or higher
	for (auto creatureEntry : creatures)
	{
		if (LIBRARY->creatures()->getById(creatureEntry.second.at(0))->getLevel() >= 5 && ID != Obj::REFUGEE_CAMP)
		{
			guarded = true;
			break;
		}
	}

	if (guarded)
	{
		std::vector<std::pair<CreatureID, TQuantity>> rawGuards;
		for(const auto & entry : creatures)
			rawGuards.emplace_back(entry.second.at(0), cb->getCreatureBaseGrowth(entry.second.at(0)) * 3);
		const auto counts = newHorizonsNeutralArmies::generatedCounts(*this, rawGuards);
		size_t index = 0;
		for (auto creatureEntry : creatures)
		{
			const CCreature * crea = creatureEntry.second.at(0).toCreature();
			SlotID slot = getSlotFor(crea->getId());

			if (hasStackAtSlot(slot)) //stack already exists, overwrite it
			{
				ChangeStackCount csc;
				csc.army = this->id;
				csc.slot = slot;
				csc.count = counts.at(index);
				csc.mode = ChangeValueMode::ABSOLUTE;
				gameEvents.sendAndApply(csc);
			}
			else //slot is empty, create whole new stack
			{
				InsertNewStack ns;
				ns.army = this->id;
				ns.slot = slot;
				ns.type = crea->getId();
				ns.count = counts.at(index);
				gameEvents.sendAndApply(ns);
			}
			++index;
		}
	}
}
