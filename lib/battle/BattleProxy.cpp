/*
 * BattleProxy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleProxy.h"
#include "Unit.h"

///BattleProxy

BattleProxy::BattleProxy(Subject subject_): 
	subject(std::move(subject_))
{}

BattleProxy::~BattleProxy() = default;

const IBattleInfo * BattleProxy::getBattle() const
{
	return this;
}

std::optional<PlayerColor> BattleProxy::getPlayerID() const
{
	return subject->getPlayerID();
}

VisibleHeroPolicy BattleProxy::battleGetVisibleHeroPolicy(BattleSide side) const
{
	return subject->battleGetVisibleHeroPolicy(side); // Do not recreate stripped address capability through getSideHero.
}

int32_t BattleProxy::getActiveStackID() const
{
	const auto * ret = subject->battleActiveUnit();
	if(ret)
		return ret->unitId();
	else
		return -1;
}

TStacks BattleProxy::getStacksIf(const TStackFilter & predicate) const
{
	return subject->battleGetStacksIf(predicate);
}

battle::Units BattleProxy::getUnitsIf(const battle::UnitFilter & predicate) const
{
	return subject->battleGetUnitsIf(predicate);
}

BattleField BattleProxy::getBattlefieldType() const
{
	return subject->battleGetBattlefieldType();
}

TerrainId BattleProxy::getTerrainType() const
{
	return subject->battleTerrainType();
}

IBattleInfo::ObstacleCList BattleProxy::getAllObstacles() const
{
	return subject->battleGetAllObstacles();
}

PlayerColor BattleProxy::getSidePlayer(BattleSide side) const
{
	return subject->sideToPlayer(side);
}

const CArmedInstance * BattleProxy::getSideArmy(BattleSide side) const
{
	// Army source context is nullable for a hidden side. Keep the callback's
	// direct access guard for explicit queries and invalid side arguments.
	if(subject->getBattle() && (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& !battleDoWeKnowAbout(side))
		return nullptr;
	return subject->battleGetArmyObject(side);
}

const CGHeroInstance * BattleProxy::getSideHero(BattleSide side) const
{
	return subject->battleGetFightingHero(side);
}

int BattleProxy::battleGetPerkMagicalReductionBasisPoints(const battle::Unit * unit) const
{
	return subject->battleGetPerkMagicalReductionBasisPoints(unit);
}

ui8 BattleProxy::getTacticDist() const
{
	return subject->battleTacticDist();
}

BattleSide BattleProxy::getTacticsSide() const
{
	return subject->battleGetTacticsSide();
}

const BattleDeploymentState & BattleProxy::getDeploymentState() const
{
	return subject->getBattle()->getDeploymentState();
}

int32_t BattleProxy::getRound() const
{
	return subject->battleGetRound();
}

const CGTownInstance * BattleProxy::getDefendedTown() const
{
	return subject->battleGetDefendedTown();
}

EWallState BattleProxy::getWallState(EWallPart partOfWall) const
{
	return subject->battleGetWallState(partOfWall);
}

int32_t BattleProxy::getWallStructuralHP(EWallPart partOfWall) const
{
	return subject->getWallStructuralHP(partOfWall);
}

EGateState BattleProxy::getGateState() const
{
	return subject->battleGetGateState();
}

int32_t BattleProxy::getCastSpells(BattleSide side) const
{
	return subject->battleCastSpells(side);
}

int32_t BattleProxy::getEnchanterCounter(BattleSide side) const
{
	return subject->battleGetEnchanterCounter(side);
}

bool BattleProxy::getTemporalFieldUsed(BattleSide side) const
{
	return subject->battleWasTemporalFieldUsed(side);
}

const IBonusBearer * BattleProxy::getBonusBearer() const
{
	return subject->getBonusBearer();
}
