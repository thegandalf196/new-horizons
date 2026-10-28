/*
 * NeutralTownGarrison.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewTurnProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/callback/CGameInfoCallback.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/StackLocation.h"


void NewTurnProcessor::updateNeutralTownGarrison(const CGTownInstance * t, int currentWeek) const
{
	assert(t);
	assert(!t->getOwner().isValidPlayer());

	constexpr int randomRollsCounts = 3; // H3 makes around 3 random rolls to make simple bell curve distribution
	constexpr int upgradeChance = 5; // Chance for a unit to get an upgrade
	constexpr int growthChanceFort = 80; // Chance for growth to occur in towns with fort built
	constexpr int growthChanceVillage = 40; // Chance for growth to occur in towns without fort

	const auto & takeFromAvailable = [this, t](CreatureID creatureID)
	{
		int tierToSubstract = -1;
		for (int i = 0; i < t->getTown()->creatures.size(); ++i)
			if (vstd::contains(t->getTown()->creatures[i], creatureID))
				tierToSubstract = i;

		if (tierToSubstract == -1)
			return; // impossible?

		int creaturesAvailable = t->creatures[tierToSubstract].first;
		int creaturesRecruited = gameHandler->gameInfo().getCreatureBaseGrowth(creatureID);
		int creaturesLeft = std::max(0, creaturesAvailable - creaturesRecruited);

		if (creaturesLeft != creaturesAvailable)
		{
			SetAvailableCreatures sac;
			sac.tid = t->id;
			sac.creatures = t->creatures;
			sac.creatures[tierToSubstract].first = creaturesLeft;
			gameHandler->sendAndApply(sac);
		}
	};

	int growthChance = t->hasFort()	? growthChanceFort : growthChanceVillage;
	int growthRoll = gameHandler->getRandomGenerator().nextInt(0, 99);

	if (growthRoll >= growthChance)
		return;

	int tierRoll = 0;
	for(int i = 0; i < randomRollsCounts; ++i)
		tierRoll += gameHandler->getRandomGenerator().nextInt(0, currentWeek);

	// NOTE: determined by observing H3 games, might not match H3 100%
	int tierToGrow = std::clamp(tierRoll / randomRollsCounts, 0, 6) + 1;

	bool upgradeUnit = gameHandler->getRandomGenerator().nextInt(0, 99) < upgradeChance;

	// Check if town garrison already has unit of specified tier
	for(const auto & slot : t->Slots())
	{
		const auto * creature = slot.second->getCreature();

		if (creature->getFactionID() != t->getFactionID())
			continue;

		if (creature->getLevel() != tierToGrow)
			continue;

		StackLocation stackLocation(t->id, slot.first);
		const int growth = gameHandler->gameInfo().getCreatureBaseGrowth(creature->getId());
		gameHandler->changeStackCount(stackLocation, newHorizonsNeutralArmies::generatedCount(*t, growth), ChangeValueMode::RELATIVE);
		takeFromAvailable(growth);

		if (upgradeUnit && !creature->upgrades.empty())
		{
			CreatureID upgraded = *RandomGeneratorUtil::nextItem(creature->upgrades, gameHandler->getRandomGenerator());
			gameHandler->changeStackType(stackLocation, upgraded.toCreature());
		}
		else
			gameHandler->changeStackType(stackLocation, creature);
		return;
	}

	// No existing creatures in garrison, but we have a free slot we can use
	SlotID freeSlotID = t->getFreeSlot();
	if (freeSlotID.validSlot())
	{
		for (auto const & tierVector : t->getTown()->creatures)
		{
			CreatureID baseCreature	= tierVector.at(0);

			if (baseCreature.toEntity(LIBRARY)->getLevel() != tierToGrow)
				continue;

			StackLocation stackLocation(t->id, freeSlotID);

			if (upgradeUnit && !baseCreature.toCreature()->upgrades.empty())
			{
				CreatureID upgraded = *RandomGeneratorUtil::nextItem(baseCreature.toCreature()->upgrades, gameHandler->getRandomGenerator());
				const int growth = gameHandler->gameInfo().getCreatureBaseGrowth(upgraded);
				gameHandler->insertNewStack(stackLocation, upgraded.toCreature(), newHorizonsNeutralArmies::generatedCount(*t, growth));
				takeFromAvailable(growth);
			}
			else
			{
				const int growth = gameHandler->gameInfo().getCreatureBaseGrowth(baseCreature);
				gameHandler->insertNewStack(stackLocation, baseCreature.toCreature(), newHorizonsNeutralArmies::generatedCount(*t, growth));
				takeFromAvailable(growth);
			}

			return;
		}
	}
}
