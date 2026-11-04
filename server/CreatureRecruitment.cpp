/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/mapObjects/CGDwelling.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/NewHorizonsEstates.h"
#include "../lib/entities/hero/NewHorizonsLeadership.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/callback/CGameInfoCallback.h"
#include "../lib/CPlayerState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/gameState/GameStatistics.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/StackLocation.h"
#include "../lib/spells/NewHorizonsMagic.h"
#include "../lib/IGameSettings.h"
#include "../lib/StartInfo.h"

#define COMPLAIN_RET_FALSE_IF(cond, txt) do {if (cond){complain(txt); return false;}} while(0)

bool CGameHandler::recruitCreatures(ObjectInstanceID objid, ObjectInstanceID dstid, CreatureID crid, int32_t cram,
	int32_t fromLvl, PlayerColor player, ObjectInstanceID portalTownId)
{
	const auto * dwelling = dynamic_cast<const CGDwelling *>(gameInfo().getObj(objid));
	const auto * town = dynamic_cast<const CGTownInstance *>(gameInfo().getObj(objid));
	const auto * army = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(dstid));
	const auto * hero = dynamic_cast<const CGHeroInstance *>(gameInfo().getObj(dstid));
	const auto * portalTown = portalTownId == ObjectInstanceID::NONE ? nullptr : gameInfo().getTown(portalTownId);
	const auto * c = crid.toCreature();

	COMPLAIN_RET_FALSE_IF(!c, "Cannot recruit: invalid creature!");
	const bool warMachine = c->warMachine != ArtifactID::NONE;

	// Destination and source checks below establish the actual purchasing hero.

	COMPLAIN_RET_FALSE_IF(!dwelling || !army, "Cannot recruit: invalid object!");
	COMPLAIN_RET_FALSE_IF(army->getOwner() != player, "Cannot recruit: destination not owned!");
	COMPLAIN_RET_FALSE_IF(dwelling->getOwner() != player && dwelling->getOwner() != PlayerColor::UNFLAGGABLE, "Cannot recruit: dwelling not owned!");

	if (portalTownId != ObjectInstanceID::NONE)
	{
		const bool externalDwelling = dwelling->ID == Obj::CREATURE_GENERATOR1 || dwelling->ID == Obj::CREATURE_GENERATOR4;
		const bool destinationBelongsToPortal = portalTown
			&& (army == portalTown
				|| (hero && (hero == portalTown->getVisitingHero() || hero == portalTown->getGarrisonHero())));
		COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::rulesActive(gameInfo().getMagicRules())
			|| !portalTown || portalTown->getOwner() != player
			|| !portalTown->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING)
			|| portalTown->portalSourceDwellingId != objid || dwelling->getOwner() != player,
			"Portal source is not selected by an owned Portal of Summoning!");
		COMPLAIN_RET_FALSE_IF(!externalDwelling,
			"Portal recruitment requires an external creature dwelling!");
		COMPLAIN_RET_FALSE_IF(warMachine,
			"Portal recruitment cannot recruit war machines!");
		COMPLAIN_RET_FALSE_IF(!destinationBelongsToPortal || army->getOwner() != player
			|| (hero && hero->getOwner() != player),
			"Portal recruitment destination must be the town or its visiting/garrisoned hero!");
	}
	else if (town)
	{
		COMPLAIN_RET_FALSE_IF(town != army && !hero, "Cannot recruit: invalid destination!");
		COMPLAIN_RET_FALSE_IF(hero != town->getGarrisonHero() && hero != town->getVisitingHero(), "Cannot recruit: can only recruit to town or hero in town!!");
	}
	else
	{
		COMPLAIN_RET_FALSE_IF(getVisitingHero(dwelling) != hero, "Cannot recruit: can only recruit by visiting hero!");
		COMPLAIN_RET_FALSE_IF(!hero || hero->getOwner() != player, "Cannot recruit: can only recruit to owned hero!");
	}

	//verify
	bool found = false;
	int level = 0;

	for (; level < dwelling->creatures.size(); level++) //iterate through all levels
	{
		if ((fromLvl != -1) && (level !=fromLvl))
			continue;
		const auto &cur = dwelling->creatures.at(level); //current level info <amount, list of cr. ids>
		int i = 0;
		for (; i < cur.second.size(); i++) //look for crid among available creatures list on current level
			if (cur.second.at(i) == crid)
				break;

		if (i < cur.second.size())
		{
			found = true;
			cram = std::min<int32_t>(cram, cur.first); //reduce recruited amount up to available amount
			break;
		}
	}
	SlotID slot = newHorizonsHeroes::recruitmentSlot(army, crid, cram);
	const auto * buyer = newHorizonsEstates::recruitmentBuyer(army);
	const auto & resources = gameInfo().getPlayerState(army->tempOwner)->resources;
	const auto maxAffordableAmount = dwelling->getRecruitmentAmount(crid, resources, cram, buyer);

	if((!found && complain("Cannot recruit: no such creatures!"))
		|| (cram > maxAffordableAmount && complain("Cannot recruit: lack of resources!"))
		|| (cram <= 0 && complain("Cannot recruit: cram <= 0!"))
		|| (!slot.validSlot() && !warMachine && complain("Cannot recruit: no available slot!")))
	{
		return false;
	}
	if(!warMachine)
	{
		const int64_t recruitedStackSize = army->hasStackAtSlot(slot)
			? static_cast<int64_t>(army->getStackCount(slot)) + static_cast<int64_t>(cram)
			: static_cast<int64_t>(cram);
		if(!validateLeadershipStack(army, crid, recruitedStackSize, "recruit"))
			return false;
	}

	if(warMachine)
	{
		const auto artId = c->warMachine;
		const auto * art = artId.toArtifact();
		COMPLAIN_RET_FALSE_IF(!hero, "Only hero can buy war machines");
		COMPLAIN_RET_FALSE_IF(artId == ArtifactID::CATAPULT, "Catapult cannot be recruited!");
		COMPLAIN_RET_FALSE_IF(nullptr == art, "Invalid war machine artifact");
		COMPLAIN_RET_FALSE_IF(hero->hasArt(artId),"Hero already has this machine!");

	}

	//recruit
	TResources cost = dwelling->getTotalRecruitmentCost(crid, cram, buyer);
	giveResources(army->tempOwner, -cost);
	statistics->getPlayerAccumulator(army->tempOwner).spentResourcesForArmy += cost;

	SetAvailableCreatures sac;
	sac.tid = objid;
	sac.creatures = dwelling->creatures;
	sac.creatures[level].first -= cram;
	sendAndApply(sac);

	if (warMachine)
	{
		ArtifactID artId = c->warMachine;
		const CArtifact * art = artId.toArtifact();


		bool hasFreeSlot = false;
		for(auto possibleSlot : art->getPossibleSlots().at(ArtBearer::HERO))
			if (hero->getArt(possibleSlot) == nullptr)
				hasFreeSlot = true;

		if (!hasFreeSlot)
		{
			auto possibleSlot = art->getPossibleSlots().at(ArtBearer::HERO).front();
			removeArtifact(ArtifactLocation(hero->id, possibleSlot));
		}
		return giveHeroNewArtifact(hero, artId, ArtifactPosition::FIRST_AVAILABLE);
	}
	else
	{
		if(!recruitToSlot(StackLocation(army->id, slot), c, cram))
			throw std::runtime_error("Prevalidated direct recruitment was rejected");
	}
	return true;
}
