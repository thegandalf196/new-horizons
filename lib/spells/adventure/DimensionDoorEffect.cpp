/*
 * DimensionDoorEffect.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "DimensionDoorEffect.h"


#include "../../IGameSettings.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapping/TerrainTile.h"
#include "../../networkPacks/PacksForClient.h"
#include "../CSpell.h"
#include "../NewHorizonsMagic.h"

namespace
{
bool isLegalNewHorizonsDestination(const IGameInfoCallback * cb, const spells::Caster * caster,
	const int3 & source, const int3 & destination)
{
	if(!cb || !caster || !cb->isInTheMap(source) || !cb->isInTheMap(destination))
		return false;

	if(source.z != destination.z || source.dist(destination, int3::DIST_2D) > 8)
		return false;

	if(!cb->isVisibleFor(destination, caster->getCasterOwner()))
		return false;

	const TerrainTile * dest = cb->getTileUnchecked(destination);
	const TerrainTile * curr = cb->getTileUnchecked(source);
	return dest && curr && dest->isClear(curr);
}
}

DimensionDoorEffect::DimensionDoorEffect(const CSpell * s, const JsonNode & config)
	: AdventureSpellRangedEffect(config)
	, owner(s)
	, cursor(config["cursor"].String())
	, cursorGuarded(config["cursorGuarded"].String())
	, movementPointsRequired(config["movementPointsRequired"].Integer())
	, movementPointsTaken(config["movementPointsTaken"].Integer())
	, waterLandFailureTakesPoints(config["waterLandFailureTakesPoints"].Bool())
	, exposeFow(config["exposeFow"].Bool())
{
}

int DimensionDoorEffect::getMovementPointsRequired() const
{
	return movementPointsRequired;
}

int DimensionDoorEffect::getMovementPointsTaken() const
{
	return movementPointsTaken;
}

int DimensionDoorEffect::getMovementPointsTaken(const spells::Caster * caster, int remainingMovement) const
{
	const int nonnegativeRemaining = std::max(0, remainingMovement);
	if(usesNewHorizonsRules(caster))
		return nonnegativeRemaining;

	return std::min(nonnegativeRemaining, std::max(0, movementPointsTaken));
}

bool DimensionDoorEffect::usesNewHorizonsRules(const spells::Caster * caster) const
{
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	return hero && owner && newHorizonsMagic::isAdventureSpell(hero->getMagicRules(), owner->id);
}

bool DimensionDoorEffect::doesWaterLandFailureTakePoints() const
{
	return waterLandFailureTakesPoints;
}

bool DimensionDoorEffect::doesExposeFogOfWar() const
{
	return exposeFow;
}

std::string DimensionDoorEffect::getCursorForTarget(const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(usesNewHorizonsRules(caster) && !cb->isVisibleFor(pos, caster->getCasterOwner()))
		return cursor;

	if(!cb->getSettings().getBoolean(EGameSettings::SPELLS_DIMENSION_DOOR_TRIGGERS_GUARDS))
		return cursor;

	// A hidden landing may be invalid or safe; only visible guarded landings need an attack cursor.
	if(!exposeFow && !cb->isVisibleFor(pos, caster->getCasterOwner()))
		return cursor;

	if(!cb->isTileGuardedUnchecked(pos))
		return cursor;

	return cursorGuarded;
}

bool DimensionDoorEffect::canBeCastImpl(spells::Problem & problem, const IGameInfoCallback * cb, const spells::Caster * caster) const
{
	if(!caster || !caster->getHeroCaster())
		return false;

	if(caster->getHeroCaster()->movementPointsRemaining() <= movementPointsRequired)
	{
		problem.add(MetaString::createFromTextID("core.genrltxt.125"));
		return false;
	}

	return true;
}

bool DimensionDoorEffect::isTargetInRange(const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(!usesNewHorizonsRules(caster))
		return AdventureSpellRangedEffect::isTargetInRange(cb, caster, pos);

	const auto * hero = caster->getHeroCaster();
	return isValidTargetFrom(cb, caster, hero->getSightCenter(), pos);
}

bool DimensionDoorEffect::isValidTargetFrom(const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & source, const int3 & destination) const
{
	if(usesNewHorizonsRules(caster))
		return isLegalNewHorizonsDestination(cb, caster, source, destination);

	if(!cb || !caster)
		return false;

	if(!AdventureSpellRangedEffect::isValidTargetFrom(cb, caster, source, destination))
		return false;

	const TerrainTile * dest = cb->getTileUnchecked(destination);
	const TerrainTile * curr = cb->getTileUnchecked(source);

	if(!dest)
		return false;

	if(!curr)
		return false;

	if(exposeFow)
	{
		if(!dest->isClear(curr))
			return false;
	}
	else
	{
		if(dest->blocked())
			return false;
	}

	return true;
}

bool DimensionDoorEffect::canBeCastAtImpl(spells::Problem & problem, const IGameInfoCallback * cb, const spells::Caster * caster, const int3 & pos) const
{
	if(!caster || !caster->getHeroCaster())
		return false;

	return isValidTargetFrom(cb, caster, caster->getHeroCaster()->getSightCenter(), pos);
}

ESpellCastResult DimensionDoorEffect::applyAdventureEffects(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const auto * hero = parameters.caster ? parameters.caster->getHeroCaster() : nullptr;
	if(!hero)
		return ESpellCastResult::ERROR;

	const bool newHorizonsRules = usesNewHorizonsRules(parameters.caster);
	const int3 casterPosition = hero->getSightCenter();
	if(newHorizonsRules && !isValidTargetFrom(env->getCb(), parameters.caster, casterPosition, parameters.pos))
		return ESpellCastResult::CANCEL;

	const TerrainTile * dest = newHorizonsRules
		? env->getCb()->getTileUnchecked(parameters.pos)
		: env->getCb()->getTile(parameters.pos);
	const TerrainTile * curr = newHorizonsRules
		? env->getCb()->getTileUnchecked(casterPosition)
		: env->getCb()->getTile(casterPosition);

	if(!dest->isClear(curr))
	{
		InfoWindow iw;
		iw.player = parameters.caster->getCasterOwner();

		// tile is either blocked or not possible to move (e.g. water <-> land)
		if(waterLandFailureTakesPoints)
		{
			// SOD: DD to such "wrong" terrain results in mana and move points spending, but fails to move hero
			iw.text = MetaString::createFromTextID("core.genrltxt.70"); // Dimension Door failed!
			env->apply(iw);
			// no return - resources will be spent
		}
		else
		{
			// HotA: game will show error message without taking mana or move points, even when DD into terra incognita
			iw.text = MetaString::createFromTextID("vcmi.dimensionDoor.seaToLandError");
			env->apply(iw);
			return ESpellCastResult::CANCEL;
		}
	}

	SetMovePoints smp;
	smp.hid = ObjectInstanceID(parameters.caster->getCasterUnitId());
	const int remainingMovement = hero->movementPointsRemaining();
	smp.val = std::max(0, remainingMovement - getMovementPointsTaken(parameters.caster, remainingMovement));
	env->apply(smp);

	return ESpellCastResult::OK;
}

void DimensionDoorEffect::endCast(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	const auto * hero = parameters.caster ? parameters.caster->getHeroCaster() : nullptr;
	if(!hero)
		return;

	const int3 casterPosition = hero->getSightCenter();
	if(usesNewHorizonsRules(parameters.caster))
	{
		if(isValidTargetFrom(env->getCb(), parameters.caster, casterPosition, parameters.pos))
			env->moveHero(ObjectInstanceID(parameters.caster->getCasterUnitId()), hero->convertFromVisitablePos(parameters.pos), EMovementMode::DIMENSION_DOOR);
		return;
	}

	const TerrainTile * dest = env->getCb()->getTile(parameters.pos);
	const TerrainTile * curr = env->getCb()->getTile(casterPosition);

	if(dest->isClear(curr))
		env->moveHero(ObjectInstanceID(parameters.caster->getCasterUnitId()), hero->convertFromVisitablePos(parameters.pos), EMovementMode::DIMENSION_DOOR);
}
