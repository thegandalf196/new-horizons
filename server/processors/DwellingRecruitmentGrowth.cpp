/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../CGameHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/spells/NewHorizonsMagic.h"

void CGameHandler::setPortalDwelling(const CGTownInstance * town, bool forced, bool clear)
{// bool forced = true - if creature should be replaced, if false - only if no creature was set
	// The New Horizons Portal links to a real owned external dwelling instead
	// of maintaining the legacy random bonus row in each town.
	if(newHorizonsMagic::rulesActive(gameInfo().getMagicRules()))
		return;

	const PlayerState * p = gameInfo().getPlayerState(town->tempOwner);
	if (!p)
	{
		assert(town->tempOwner == PlayerColor::NEUTRAL);
		return;
	}

	if (forced || town->creatures.at(town->getTown()->creatures.size()).second.empty())//we need to change creature
		{
			SetAvailableCreatures ssi;
			ssi.tid = town->id;
			ssi.creatures = town->creatures;
			ssi.creatures[town->getTown()->creatures.size()].second.clear();//remove old one

			std::set<CreatureID> availableCreatures;
			for (const auto & dwelling : p->getOwnedObjects())
			{
				const auto & dwellingCreatures = dwelling->asOwnable()->providedCreatures();
				availableCreatures.insert(dwellingCreatures.begin(), dwellingCreatures.end());
			}

			if (availableCreatures.empty())
				return;

			CreatureID creatureId = *RandomGeneratorUtil::nextItem(availableCreatures, getRandomGenerator());

			if (clear)
			{
				ssi.creatures[town->getTown()->creatures.size()].first = newHorizonsRecruitmentGrowth::generatedAmount(
					std::max(1, gameInfo().getCreatureBaseGrowth(creatureId) / 2), gameInfo().getSettings());
			}
			else
			{
				ssi.creatures[town->getTown()->creatures.size()].first = newHorizonsRecruitmentGrowth::generatedAmount(
					gameInfo().getCreatureBaseGrowth(creatureId), gameInfo().getSettings());
			}
			ssi.creatures[town->getTown()->creatures.size()].second.push_back(creatureId);
			sendAndApply(ssi);
		}
}


void CGameHandler::initializeRecruitmentForBuilding(const CGTownInstance * t, BuildingID buildingID)
{
	if(buildingID.isDwelling())
	{
		int level = BuildingID::getLevelIndexFromDwelling(buildingID);
		int upgradeNumber = BuildingID::getUpgradeNoFromDwelling(buildingID);

		if(upgradeNumber >= t->getTown()->creatures.at(level).size())
		{
			complain(boost::str(boost::format("Error encountered when building dwelling (bid=%s):"
												"no creature found (upgrade number %d, level %d!")
											% buildingID % upgradeNumber % level));
			return;
		}

		const CCreature * crea = t->getTown()->creatures.at(level).at(upgradeNumber).toCreature();

		SetAvailableCreatures ssi;
		ssi.tid = t->id;
		ssi.creatures = t->creatures;
		if (ssi.creatures[level].second.empty()) // first creature in a dwelling
			ssi.creatures[level].first = newHorizonsRecruitmentGrowth::generatedAmount(
				gameInfo().getCreatureBaseGrowth(crea->getId()), gameInfo().getSettings());
		ssi.creatures[level].second.push_back(crea->getId());
		sendAndApply(ssi);
	}
	if(t->getTown()->buildings.at(buildingID)->subId == BuildingSubID::PORTAL_OF_SUMMONING)
	{
		setPortalDwelling(t);
	}
}
