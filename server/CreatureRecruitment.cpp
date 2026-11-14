/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "CommandArmyAdmission.h"
#include "../lib/mapObjects/CGDwelling.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/NewHorizonsEstates.h"
#include "../lib/mapObjects/TownRecruitmentOffers.h"
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

namespace
{
std::optional<size_t> recruitmentRow(const CGDwelling & dwelling, CreatureID creature,
	int32_t requestedRow, PlayerColor player)
{
	for(size_t row = 0; row < dwelling.creatures.size(); ++row)
	{
		if(requestedRow != -1 && static_cast<int64_t>(row) != requestedRow)
			continue;
		const auto offer = townRecruitmentOffers::query(dwelling, row, player);
		if(offer && vstd::contains(offer->choices, creature))
			return row;
	}
	return std::nullopt;
}

std::optional<newHorizonsHeroes::CommandArmyPlan> prepareRecruitmentPlacement(
	const CArmedInstance * army, const CGHeroInstance * hero, CreatureID crid, int32_t cram,
	bool warMachine, SlotID & slot)
{
	slot = newHorizonsHeroes::recruitmentSlot(army, crid, cram);
	std::optional<newHorizonsHeroes::CommandArmyPlan> commandPlan;
	if(!warMachine && hero && hero->getArmyCommand())
	{
		const std::array incoming{newHorizonsHeroes::RecruitmentSlot{SlotID(0), crid, cram}};
		commandPlan = newHorizonsHeroes::planCommandArmyAddition(*hero,
			newHorizonsHeroes::commandArmySlots(*hero), incoming);
		if(commandPlan)
			for(const auto & move : commandPlan->moves)
				if(!move.withinDestination) slot = move.destination;
	}
	return commandPlan;
}

const char * warMachineRecruitmentError(const CGHeroInstance * hero, ArtifactID artId)
{
	const auto * art = artId.toArtifact();
	if(!hero)
		return "Only hero can buy war machines";
	if(artId == ArtifactID::CATAPULT)
		return "Catapult cannot be recruited!";
	if(nullptr == art)
		return "Invalid war machine artifact";
	if(hero->hasArt(artId))
		return "Hero already has this machine!";
	return nullptr;
}
}

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

	// Re-evaluate the source's live legal choices before placement or payment.
	const auto row = recruitmentRow(*dwelling, crid, fromLvl, player);
	COMPLAIN_RET_FALSE_IF(!row, "Cannot recruit: no such creatures!");
	const auto level = *row;
	cram = static_cast<int32_t>(std::min<int64_t>(cram, dwelling->creatures[level].first));
	COMPLAIN_RET_FALSE_IF(cram <= 0, "Cannot recruit: cram <= 0!");
	SlotID slot;
	auto commandPlan = prepareRecruitmentPlacement(army, hero, crid, cram, warMachine, slot);
	const auto * buyer = newHorizonsEstates::recruitmentBuyer(army);
	const auto & resources = gameInfo().getPlayerState(army->tempOwner)->resources;
	const auto maxAffordableAmount = dwelling->getRecruitmentAmount(crid, resources, cram, buyer);

	if((cram > maxAffordableAmount && complain("Cannot recruit: lack of resources!"))
		|| ((!slot.validSlot() || (hero && hero->getArmyCommand() && !commandPlan)) && !warMachine
			&& complain("Cannot recruit: no legal arrangement in unlocked army slots!")))
	{
		return false;
	}
	if(!warMachine)
	{
		const int64_t recruitedStackSize = army->hasStackAtSlot(slot)
			? static_cast<int64_t>(army->getStackCount(slot)) + static_cast<int64_t>(cram)
			: static_cast<int64_t>(cram);
		if(!commandPlan && !commandArmyAdmission::validateSlot(*this, army, slot, crid, recruitedStackSize, "recruit"))
			return false;
	}

	if(warMachine)
	{
		const auto * error = warMachineRecruitmentError(hero, c->warMachine);
		COMPLAIN_RET_FALSE_IF(error, error);
	}

	//recruit
	if(commandPlan)
		commandArmyAdmission::rearrange(*this, *army, *commandPlan);
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
