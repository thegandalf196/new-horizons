/*
 * SummonBoatEffect.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "SummonBoatEffect.h"

#include "../CSpell.h"

#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/MiscObjects.h"
#include "../../mapping/CMap.h"
#include "../../modding/IdentifierStorage.h"
#include "../../networkPacks/PacksForClient.h"
#include "../NewHorizonsMagic.h"

namespace
{
const int3 noSummonBoatTarget(-1, -1, -1);

bool isNoSummonBoatTarget(const int3 & pos)
{
	return pos == noSummonBoatTarget;
}

bool isLegalNewHorizonsDestination(const IGameInfoCallback * cb, const CGHeroInstance * hero, const int3 & pos)
{
	if(!cb || !hero)
		return false;

	const int3 origin = hero->visitablePos();
	const int64_t dx = static_cast<int64_t>(pos.x) - origin.x;
	const int64_t dy = static_cast<int64_t>(pos.y) - origin.y;

	if(pos.z != origin.z || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
		return false;

	if(!cb->isInTheMap(pos) || !cb->isVisibleFor(pos, hero->getCasterOwner()))
		return false;

	const TerrainTile * tile = cb->getTile(pos, false);
	return tile && tile->isWater() && !tile->blocked() && !tile->visitable();
}

int3 findFirstLegalNewHorizonsDestination(const IGameInfoCallback * cb, const CGHeroInstance * hero)
{
	if(!cb || !hero)
		return noSummonBoatTarget;

	std::vector<int3> offsets;
	hero->getOutOffsets(offsets);

	for(const auto & offset : offsets)
	{
		const int3 candidate = hero->visitablePos() + offset;
		if(isLegalNewHorizonsDestination(cb, hero, candidate))
			return candidate;
	}

	return noSummonBoatTarget;
}

const CGBoat * findNearestAvailableBoat(const CMap * map, const CGHeroInstance * hero)
{
	if(!map || !hero)
		return nullptr;

	const CGBoat * nearest = nullptr;
	double distance = 0;
	for(const auto & boat : map->getObjects<CGBoat>())
	{
		if(boat->getBoardedHero() || boat->layer != EPathfindingLayer::SAIL)
			continue;

		const double candidateDistance = boat->visitablePos().dist2d(hero->visitablePos());
		if(!nearest || candidateDistance < distance)
		{
			nearest = boat;
			distance = candidateDistance;
		}
	}

	return nearest;
}

void showNoAvailableBoat(SpellCastEnvironment * env, const spells::Caster * caster)
{
	InfoWindow info;
	info.player = caster->getCasterOwner();
	info.text.appendTextID("core.genrltxt.335"); //There are no boats to summon.
	env->apply(info);
}
}

SummonBoatEffect::SummonBoatEffect(const CSpell * s, const JsonNode & config)
	: owner(s)
	, useExistingBoat(config["useExistingBoat"].Bool())
{
	if (!config["createdBoat"].isNull())
	{
		LIBRARY->identifiers()->requestIdentifier("core:boat", config["createdBoat"], [this](int32_t boatTypeID)
		{
			createdBoat = BoatId(boatTypeID);
		});
	}

}

bool SummonBoatEffect::requiresExistingBoat(const spells::Caster * caster) const
{
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	return hero && newHorizonsMagic::isAdventureSpell(hero->getMagicRules(), owner->id);
}

bool SummonBoatEffect::requiresTargetSelection(const spells::Caster * caster) const
{
	return requiresExistingBoat(caster);
}

bool SummonBoatEffect::isTargetInRange(const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(!requiresExistingBoat(caster))
		return true;

	return isLegalNewHorizonsDestination(cb, caster->getHeroCaster(), pos);
}

bool SummonBoatEffect::canBeCastAtImpl(spells::Problem &, const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(!requiresExistingBoat(caster))
		return true;

	if(isNoSummonBoatTarget(pos))
		return !isNoSummonBoatTarget(findFirstLegalNewHorizonsDestination(cb, caster->getHeroCaster()));

	return isLegalNewHorizonsDestination(cb, caster->getHeroCaster(), pos);
}

std::string SummonBoatEffect::getCursorForTarget(const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(requiresExistingBoat(caster) && isLegalNewHorizonsDestination(cb, caster->getHeroCaster(), pos))
		return "mapTurn1Sail";

	return {};
}

bool SummonBoatEffect::canCreateNewBoat(const spells::Caster * caster) const
{
	return createdBoat != BoatId::NONE && !requiresExistingBoat(caster);
}

int SummonBoatEffect::getSuccessChance(const spells::Caster * caster) const
{
	const auto schoolLevel = caster->getSpellSchoolLevel(owner);
	return owner->getLevelPower(schoolLevel);
}

ESpellCastResult SummonBoatEffect::beginCast(
	SpellCastEnvironment * env,
	const AdventureSpellCastParameters & parameters,
	const AdventureSpellMechanics &) const
{
	if(!requiresExistingBoat(parameters.caster))
		return ESpellCastResult::OK;

	if(findNearestAvailableBoat(env->getMap(), parameters.caster->getHeroCaster()))
		return ESpellCastResult::OK;

	showNoAvailableBoat(env, parameters.caster);
	return ESpellCastResult::ERROR;
}

bool SummonBoatEffect::canBeCastImpl(spells::Problem & problem, const IGameInfoCallback * cb, const spells::Caster * caster) const
{
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	if(!hero)
		return false;

	if(hero->inBoat())
	{
		MetaString message = MetaString::createFromTextID("core.genrltxt.333");
		message.replaceTextID(caster->getCasterNameTextID());
		problem.add(std::move(message));
		return false;
	}

	const bool mustUseExistingBoat = requiresExistingBoat(caster);
	const int3 summonPos = mustUseExistingBoat
		? findFirstLegalNewHorizonsDestination(cb, hero)
		: hero->bestLocation();

	if((mustUseExistingBoat && isNoSummonBoatTarget(summonPos)) || (!mustUseExistingBoat && summonPos.x < 0))
	{
		MetaString message = MetaString::createFromTextID("core.genrltxt.334");
		message.replaceTextID(caster->getCasterNameTextID());
		problem.add(std::move(message));
		return false;
	}

	return true;
}

ESpellCastResult SummonBoatEffect::applyAdventureEffects(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const auto * hero = parameters.caster ? parameters.caster->getHeroCaster() : nullptr;
	if(!hero)
		return ESpellCastResult::ERROR;

	const bool mustUseExistingBoat = requiresExistingBoat(parameters.caster);
	int3 summonPos = noSummonBoatTarget;
	if(mustUseExistingBoat)
	{
		summonPos = isNoSummonBoatTarget(parameters.pos)
			? findFirstLegalNewHorizonsDestination(env->getCb(), hero)
			: parameters.pos;

		if(!isLegalNewHorizonsDestination(env->getCb(), hero, summonPos))
			return ESpellCastResult::ERROR;
	}

	//check if spell works at all
	if(env->getRNG()->nextInt(0, 99) >= getSuccessChance(parameters.caster)) //power is % chance of success
	{
		InfoWindow iw;
		iw.player = parameters.caster->getCasterOwner();
		iw.text.appendTextID("core.genrltxt.336"); //%s tried to summon a boat, but failed.
		iw.text.replaceTextID(parameters.caster->getCasterNameTextID());
		env->apply(iw);
		return ESpellCastResult::OK;
	}

	// New Horizons uses the captured neutral Adventure Spell rules. It always
	// retrieves an existing boat, regardless of legacy mastery configuration.
	const CGBoat * nearest = (useExistingBoat || mustUseExistingBoat)
		? findNearestAvailableBoat(env->getMap(), hero)
		: nullptr;
	if(!mustUseExistingBoat)
		summonPos = hero->bestLocation();

	if(nullptr != nearest) //we found boat to summon
	{
		ChangeObjPos cop;
		cop.objid = nearest->id;
		cop.nPos = summonPos;
		cop.initiator = parameters.caster->getCasterOwner();
		env->apply(cop);
	}
	else if(!canCreateNewBoat(parameters.caster)) //no available boat and creation is not allowed
	{
		showNoAvailableBoat(env, parameters.caster);
		return ESpellCastResult::ERROR;
	}
	else //create boat
	{
		env->createBoat(summonPos, createdBoat, parameters.caster->getCasterOwner());
	}
	return ESpellCastResult::OK;
}
