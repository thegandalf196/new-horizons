/*
* BuyArmy.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "../StdInc.h"
#include "BuyArmy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"


namespace NK2AI
{

using namespace Goals;

bool BuyArmy::operator==(const BuyArmy & other) const
{
	return town == other.town && objid == other.objid;
}

std::string BuyArmy::toString() const
{
	return "Buy army at " + town->getNameTextID();
}

void BuyArmy::accept(AIGateway * aiGw)
{
	// Muster consumes the town's weekly recruitment action.  Wait for the
	// authoritative stock/marker packets before planning ordinary purchases;
	// otherwise this same goal can buy the pre-Muster pool in the same turn.
	if(aiGw->hasPendingMuster(town))
	{
		logAi->debug("Deferring BuyArmy at town %s until its Muster result is replicated", town->getNameTextID());
		return;
	}

	ui64 valueBought = 0;
	//buy the stacks with largest AI value

	auto upgradeSuccessful = aiGw->makePossibleUpgrades(town);

	auto armyToBuy = aiGw->nullkiller->armyManager->getArmyAvailableToBuy(town->getUpperArmy(), town);
	struct RecruitmentCandidate
	{
		const CGDwelling * source = nullptr;
		ObjectInstanceID portalTownId = ObjectInstanceID::NONE;
		creInfo creature;
	};
	std::vector<RecruitmentCandidate> candidates;
	bool portalSourceSelectionSubmitted = false;
	candidates.reserve(armyToBuy.size());
	for(const auto & creature : armyToBuy)
	{
		// The legacy extra town row is not Portal stock. Portal candidates below
		// are scored directly from the real selected external dwelling.
		if(newHorizonsMagic::rulesActive(aiGw->cc->getMagicRules()) && creature.level >= 0
			&& static_cast<size_t>(creature.level) >= town->getTown()->creatures.size())
			continue;
		candidates.push_back({town, ObjectInstanceID::NONE, creature});
	}

	const auto * portalDwelling = aiGw->getBestPortalRecruitmentDwelling(town, town->getUpperArmy());
	if(portalDwelling)
	{
		auto portalArmy = aiGw->nullkiller->armyManager->getArmyAvailableToBuy(
			town->getUpperArmy(), portalDwelling);
		for(const auto & creature : portalArmy)
		{
			if(objid != CreatureID::NONE && creature.creID.getNum() != objid)
				continue;
			candidates.push_back({portalDwelling, town->id, creature});
		}
	}

	if(candidates.empty())
	{
		if(upgradeSuccessful)
			return;

		throw cannotFulfillGoalException("No creatures to buy.");
	}

	std::stable_sort(candidates.begin(), candidates.end(), [](const RecruitmentCandidate & left, const RecruitmentCandidate & right)
	{
		return left.creature.creID.toCreature()->getAIValue() > right.creature.creID.toCreature()->getAIValue();
	});

	for(auto & candidate : candidates)
	{
		if(valueBought >= value)
			break;

		auto res = aiGw->cc->getResourceAmount();
		auto & ci = candidate.creature;

		if(objid != CreatureID::NONE && ci.creID.getNum() != objid)
			continue;

		const auto recruitCost = candidate.source->getRecruitmentCost(ci.creID);
		if(!recruitCost.empty())
			vstd::amin(ci.count, res / recruitCost);
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(town->getUpperArmy()))
		{
			if(const auto capacity = hero->getLeadershipSlotCapacity(ci.creID))
			{
				const auto slot = hero->getSlotFor(ci.creID);
				const int alreadyPresent = slot.validSlot() ? hero->getStackCount(slot) : 0;
				vstd::amin(ci.count, std::max(0, capacity->maximum - alreadyPresent));
			}
		}

		if(ci.count)
		{
			if (needsFreeSlotToRecruit(town->getUpperArmy(), ci.creID))
			{
				SlotID lowestValueSlot;
				int lowestValue = std::numeric_limits<int>::max();
				for (const auto & slot : town->getUpperArmy()->Slots())
				{
					if (slot.second->getCreatureID() != CreatureID::NONE)
					{
						int currentStackMarketValue =
							slot.second->getCreatureID().toCreature()->getFullRecruitCost().marketValue() * slot.second->getCount();

						if (slot.second->getCreatureID().toCreature()->getFactionID() == town->getFactionID())
							continue;

						if (currentStackMarketValue < lowestValue)
						{
							lowestValue = currentStackMarketValue;
							lowestValueSlot = slot.first;
						}
					}
				}
				if (lowestValueSlot.validSlot())
				{
					aiGw->cc->dismissCreature(town->getUpperArmy(), lowestValueSlot);
				}
			}
			if (town->getUpperArmy()->stacksCount() < GameConstants::ARMY_SIZE || town->getUpperArmy()->getSlotFor(ci.creID).validSlot()) //It is possible we don't scrap despite we wanted to due to not scrapping stacks that fit our faction
			{
				if(candidate.portalTownId != ObjectInstanceID::NONE && !portalSourceSelectionSubmitted)
				{
					aiGw->selectPortalRecruitmentDwelling(town, candidate.source);
					portalSourceSelectionSubmitted = true;
				}
				aiGw->cc->recruitCreatures(candidate.source, town->getUpperArmy(), ci.creID, ci.count, ci.level,
					candidate.portalTownId);
			}
			valueBought += ci.count * ci.creID.toCreature()->getAIValue();
		}
	}

	if(!valueBought)
	{
		throw cannotFulfillGoalException("No creatures to buy.");
	}

	if(town->getVisitingHero() && !town->getGarrisonHero())
	{
		aiGw->moveHeroToTile(town->visitablePos(), HeroPtr(town->getVisitingHero(), aiGw->cc.get()));
	}
}

}
