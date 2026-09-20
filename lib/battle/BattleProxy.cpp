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
	return subject->battleGetArmyObject(side);
}

const CGHeroInstance * BattleProxy::getSideHero(BattleSide side) const
{
	return subject->battleGetFightingHero(side);
}

ui8 BattleProxy::getTacticDist() const
{
	return subject->battleTacticDist();
}

BattleSide BattleProxy::getTacticsSide() const
{
	return subject->battleGetTacticsSide();
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

bool BattleProxy::getCounterspellArmed(BattleSide side) const
{
	return subject->battleWasCounterspellArmed(side);
}

int32_t BattleProxy::getMetamagicPendingCount(BattleSide side) const
{
	return subject->getBattle()->getMetamagicPendingCount(side);
}

int32_t BattleProxy::getMetamagicUsesConsumed(BattleSide side) const
{
	return subject->getBattle()->getMetamagicUsesConsumed(side);
}

bool BattleProxy::getMetamagicGrandUsed(BattleSide side) const
{
	return subject->getBattle()->getMetamagicGrandUsed(side);
}

bool BattleProxy::getMetamagicFormulaReserveUsed(BattleSide side) const
{
	return subject->getBattle()->getMetamagicFormulaReserveUsed(side);
}

bool BattleProxy::getMetamagicCountersequenceArmed(BattleSide side) const
{
	return subject->getBattle()->getMetamagicCountersequenceArmed(side);
}

SpellID BattleProxy::getMetamagicFirstSpell(BattleSide side) const
{
	return subject->getBattle()->getMetamagicFirstSpell(side);
}

uint32_t BattleProxy::getMetamagicFirstTargetUnitId(BattleSide side) const
{
	return subject->getBattle()->getMetamagicFirstTargetUnitId(side);
}

const std::vector<SpellID> & BattleProxy::getMetamagicSequenceSpells(BattleSide side) const
{
	return subject->getBattle()->getMetamagicSequenceSpells(side);
}

bool BattleProxy::getMetamagicFirstCounterspellNegated(BattleSide side) const
{
	return subject->getBattle()->getMetamagicFirstCounterspellNegated(side);
}

const IBonusBearer * BattleProxy::getBonusBearer() const
{
	return subject->getBonusBearer();
}
