/*
* BuyArmyBehavior.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "BuyArmyBehavior.h"
#include "../AIGateway.h"
#include "../AIUtility.h"
#include "../Goals/BuyArmy.h"
#include "../Engine/Nullkiller.h"

namespace NK2AI
{

using namespace Goals;

std::string BuyArmyBehavior::toString() const
{
	return "Buy army";
}

Goals::TGoalVec BuyArmyBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;
	const auto heroes = aiNk->cc->getHeroesInfo();
	if(heroes.empty())
		return tasks;

	// Simplification: Moved this call before getting into the decomposer
	// aiNk->dangerHitMap->updateHitMap();

	for(const auto town : aiNk->cc->getTownsInfo())
	{
		uint8_t closestThreat = aiNk->dangerHitMap->getTileThreat(town->visitablePos()).fastestDanger.turn;
		if(closestThreat >= 2 && aiNk->buildAnalyzer->isGoldPressureOverMax() && !town->hasBuilt(BuildingID::CITY_HALL) &&
		   aiNk->cc->canBuildStructure(town, BuildingID::CITY_HALL) != EBuildingState::FORBIDDEN)
		{
			return tasks;
		}

		auto townArmyAvailableToBuy = aiNk->armyManager->getArmyAvailableToBuyAsCCreatureSet(town, aiNk->getFreeResources());
		const auto * portalSource = aiNk->aiGw->getBestPortalRecruitmentDwelling(town, town->getUpperArmy());
		auto portalArmyAvailableToBuy = portalSource
			? aiNk->armyManager->getArmyAvailableToBuyAsCCreatureSet(portalSource, aiNk->getFreeResources())
			: nullptr;

		for(const CGHeroInstance * targetHero : heroes)
		{
			if(aiNk->heroManager->getHeroRoleOrDefaultInefficient(targetHero) == HeroRole::MAIN)
			{
				auto reinforcement = aiNk->armyManager->howManyReinforcementsCanGet(
					targetHero,
					targetHero,
					&*townArmyAvailableToBuy,
					TerrainId::NONE);

				// TODO: Mircea: Shouldn't matter if hero is MAIN when buying reinforcements if there's a threat around
				// Evaluate the entire code with the outside towns loop too.
				if(reinforcement)
					vstd::amin(reinforcement, aiNk->armyManager->howManyReinforcementsCanBuy(town->getUpperArmy(), town));

				// Portal stock belongs to the linked external dwelling, so value it
				// from that real source rather than the town's legacy display row.
				if(portalSource && portalArmyAvailableToBuy)
				{
					auto portalReinforcement = aiNk->armyManager->howManyReinforcementsCanGet(
						targetHero,
						targetHero,
						&*portalArmyAvailableToBuy,
						TerrainId::NONE);
					if(portalReinforcement)
						vstd::amin(portalReinforcement,
							aiNk->armyManager->howManyReinforcementsCanBuy(town->getUpperArmy(), portalSource));
					reinforcement = std::max(reinforcement, portalReinforcement);
				}

				if(reinforcement)
				{
					tasks.push_back(Goals::sptr(Goals::BuyArmy(town, reinforcement).setpriority(reinforcement)));
				}
			}
		}
	}

	return tasks;
}

}
