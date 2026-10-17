/*
 * CrisisCommandLiveReferences.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CrisisCommandReferences.h"
#include "IBattleState.h"

newHorizonsCrisisCommand::LiveCrisisReferences::LiveCrisisReferences(const IBattleInfo & battle) : battle(battle) {}
battle::Units newHorizonsCrisisCommand::LiveCrisisReferences::getUnitsIf(const battle::UnitFilter & predicate) const
{
	return battle.getUnitsIf(predicate);
}
const CGHeroInstance * newHorizonsCrisisCommand::LiveCrisisReferences::getSideHero(BattleSide side) const
{
	return battle.getSideHero(side);
}
const SeizeInitiativeState & newHorizonsCrisisCommand::LiveCrisisReferences::getSeizeInitiativeState() const
{
	return battle.getSeizeInitiativeState();
}
int32_t newHorizonsCrisisCommand::LiveCrisisReferences::getRound() const
{
	return battle.getRound();
}
const HeroActionAllowanceState & newHorizonsCrisisCommand::LiveCrisisReferences::getHeroActionAllowances(BattleSide side) const
{
	return battle.getHeroActionAllowances(side);
}
