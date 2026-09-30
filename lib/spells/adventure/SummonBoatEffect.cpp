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

#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/MiscObjects.h"
#include "../../mapping/CMap.h"
#include "../../modding/IdentifierStorage.h"
#include "../../networkPacks/PacksForClient.h"
#include "../NewHorizonsMagic.h"

namespace
{
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
	if(!caster->getHeroCaster())
		return false;

	if(caster->getHeroCaster()->inBoat())
	{
		MetaString message = MetaString::createFromTextID("core.genrltxt.333");
		message.replaceTextID(caster->getCasterNameTextID());
		problem.add(std::move(message));
		return false;
	}

	int3 summonPos = caster->getHeroCaster()->bestLocation();

	if(summonPos.x < 0)
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
	const bool mustUseExistingBoat = requiresExistingBoat(parameters.caster);
	const CGBoat * nearest = (useExistingBoat || mustUseExistingBoat)
		? findNearestAvailableBoat(env->getMap(), parameters.caster->getHeroCaster())
		: nullptr;

	int3 summonPos = parameters.caster->getHeroCaster()->bestLocation();

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
