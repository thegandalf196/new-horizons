/*
 * TownPortalEffect.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "TownPortalEffect.h"

#include "TownRelatedAdventureSpellEffect.h"

#include "../../CPlayerState.h"
#include "../../IGameSettings.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../mapping/CMap.h"
#include "../../networkPacks/PacksForClient.h"
#include "../CSpell.h"
#include "../NewHorizonsMagic.h"

TownPortalEffect::TownPortalEffect(const CSpell * s, const JsonNode & config)
	: TownRelatedAdventureSpellEffect(s, config["allowTownSelection"].Bool(), config["skipOccupiedTowns"].Bool())
	, movementPointsRequired(config["movementPointsRequired"].Integer())
	, movementPointsTaken(config["movementPointsTaken"].Integer())
{
}

bool TownPortalEffect::shouldOfferTownInDialog(const CGTownInstance * town) const
{
	return town->getVisitingHero() == nullptr;
}

bool TownPortalEffect::usesNearestControlledTown(const CGHeroInstance * hero) const
{
	return hero && owner && owner->id == SpellID(SpellID::TOWN_PORTAL)
		&& newHorizonsMagic::isAdventureSpell(hero->getMagicRules(), owner->id);
}

bool TownPortalEffect::townSelectionAllowed(const CGHeroInstance * hero) const
{
	return allowTownSelection && !usesNearestControlledTown(hero);
}

std::string TownPortalEffect::getTargetingHintTextId(const spells::Caster * caster) const
{
	return caster && usesNearestControlledTown(caster->getHeroCaster())
		? "new-horizons.adventure.townPortal.targetingHint" : std::string{};
}

std::vector<const CGTownInstance *> TownPortalEffect::getControlledTowns(const IGameInfoCallback & callback, const CGHeroInstance * hero) const
{
	if(!hero)
		return {};
	const auto * player = callback.getPlayerState(hero->getOwner());
	return player ? player->getTowns() : std::vector<const CGTownInstance *>{};
}

std::vector<const CGTownInstance *> TownPortalEffect::getPlayerTeamTowns(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const auto * hero = parameters.caster->getHeroCaster();
	if(usesNearestControlledTown(hero))
		return getControlledTowns(*env->getCb(), hero);
	return TownRelatedAdventureSpellEffect::getPlayerTeamTowns(env, parameters);
}

int TownPortalEffect::getMovementPointsTaken(const CGHeroInstance * hero, int remainingMovement) const
{
	const int nonnegativeRemaining = std::max(0, remainingMovement);
	if(usesNearestControlledTown(hero))
		return nonnegativeRemaining;

	return std::min(nonnegativeRemaining, std::max(0, movementPointsTaken));
}

void TownPortalEffect::configureDialogTitleAndDescription(MetaString & title, MetaString & description) const
{
	title.appendTextID("core.jktext.40");
	description.appendTextID("core.jktext.41");
}

ESpellCastResult TownPortalEffect::beginCastExtraChecks(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters, const std::vector<const CGTownInstance *> &) const
{
	if(static_cast<int>(parameters.caster->getHeroCaster()->movementPointsRemaining()) < movementPointsTaken)
	{
		InfoWindow iw;
		iw.player = parameters.caster->getCasterOwner();
		iw.text.appendTextID("core.genrltxt.125");
		env->apply(iw);
		return ESpellCastResult::CANCEL;
	}

	return ESpellCastResult::OK;
}

ESpellCastResult TownPortalEffect::applyAdventureEffects(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const CGTownInstance * destination = nullptr;

	if(!parameters.caster->getHeroCaster())
	{
		env->complain("Not a hero caster!");
		return ESpellCastResult::ERROR;
	}

	if(!townSelectionAllowed(parameters.caster->getHeroCaster()))
	{
		std::vector<const CGTownInstance *> pool = getPlayerTeamTowns(env, parameters);
		destination = findNearestTown(parameters, pool);

		if(nullptr == destination)
			return ESpellCastResult::ERROR;

		if(static_cast<int>(parameters.caster->getHeroCaster()->movementPointsRemaining()) < movementPointsRequired)
			return ESpellCastResult::ERROR;

		if(destination->getVisitingHero())
		{
			InfoWindow iw;
			iw.player = parameters.caster->getCasterOwner();
			iw.text.appendTextID("core.genrltxt.123");
			env->apply(iw);
			return ESpellCastResult::CANCEL;
		}
	}
	else if(env->getMap()->isInTheMap(parameters.pos))
	{
		const TerrainTile & tile = env->getMap()->getTile(parameters.pos);

		ObjectInstanceID topObjID = tile.topVisitableObj(false);
		const CGObjectInstance * topObj = env->getMap()->getObject(topObjID);

		if(!topObj)
		{
			env->complain("Destination tile is not visitable" + parameters.pos.toString());
			return ESpellCastResult::ERROR;
		}
		else if(topObj->ID == Obj::HERO)
		{
			env->complain("Can't teleport to occupied town at " + parameters.pos.toString());
			return ESpellCastResult::ERROR;
		}
		else if(topObj->ID != Obj::TOWN)
		{
			env->complain("No town at destination tile " + parameters.pos.toString());
			return ESpellCastResult::ERROR;
		}

		destination = dynamic_cast<const CGTownInstance *>(topObj);

		if(nullptr == destination)
		{
			env->complain("[Internal error] invalid town object at " + parameters.pos.toString());
			return ESpellCastResult::ERROR;
		}

		const auto relations = env->getCb()->getPlayerRelations(destination->tempOwner, parameters.caster->getCasterOwner());

		if(relations == PlayerRelations::ENEMIES)
		{
			env->complain("Can't teleport to enemy!");
			return ESpellCastResult::ERROR;
		}

		if(static_cast<int>(parameters.caster->getHeroCaster()->movementPointsRemaining()) < movementPointsRequired)
		{
			env->complain("This hero has not enough movement points!");
			return ESpellCastResult::ERROR;
		}

		if(destination->getVisitingHero())
		{
			env->complain("[Internal error] Can't teleport to occupied town");
			return ESpellCastResult::ERROR;
		}
	}
	else
	{
		env->complain("Invalid destination tile");
		return ESpellCastResult::ERROR;
	}

	const TerrainTile & from = env->getMap()->getTile(parameters.caster->getHeroCaster()->visitablePos());
	const TerrainTile & dest = env->getMap()->getTile(destination->visitablePos());

	if(!dest.entrableTerrain(&from))
	{
		InfoWindow iw;
		iw.player = parameters.caster->getCasterOwner();
		iw.text.appendTextID("core.genrltxt.135");
		env->apply(iw);
		return ESpellCastResult::ERROR;
	}

	return ESpellCastResult::OK;
}

void TownPortalEffect::endCast(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const CGTownInstance * destination = nullptr;

	if(!townSelectionAllowed(parameters.caster->getHeroCaster()))
	{
		std::vector<const CGTownInstance *> pool = getPlayerTeamTowns(env, parameters);
		destination = findNearestTown(parameters, pool);
	}
	else
	{
		const TerrainTile & tile = env->getMap()->getTile(parameters.pos);
		ObjectInstanceID topObjID = tile.topVisitableObj(false);
		const CGObjectInstance * topObj = env->getMap()->getObject(topObjID);

		destination = dynamic_cast<const CGTownInstance *>(topObj);
	}

	if(env->moveHero(ObjectInstanceID(parameters.caster->getCasterUnitId()), parameters.caster->getHeroCaster()->convertFromVisitablePos(destination->visitablePos()), EMovementMode::TOWN_PORTAL))
	{
		SetMovePoints smp;
		smp.hid = ObjectInstanceID(parameters.caster->getCasterUnitId());
		const auto * hero = parameters.caster->getHeroCaster();
		const int remainingMovement = static_cast<int>(hero->movementPointsRemaining());
		smp.val = std::max(0, remainingMovement - getMovementPointsTaken(hero, remainingMovement));
		env->apply(smp);
	}
}
