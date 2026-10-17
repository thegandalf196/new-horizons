/*
 * LiveElementalConsumptionContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ElementalConsumptionContext.h"
#include "IBattleState.h"
#include "../spells/NewHorizonsElementalTerrain.h"

using namespace newHorizonsElementalRebirth;

LiveElementalConsumptionContext::LiveElementalConsumptionContext(const IBattleInfo & battle) : battle(battle) {}
battle::Units LiveElementalConsumptionContext::getUnitsIf(const battle::UnitFilter & predicate) const
{
	return battle.getUnitsIf(predicate);
}
const CGHeroInstance * LiveElementalConsumptionContext::getSideHero(BattleSide side) const
{
	return battle.getSideHero(side);
}
bool LiveElementalConsumptionContext::getPhoenixSparkUsed(BattleSide side) const
{
	return battle.getPhoenixSparkUsed(side);
}
bool LiveElementalConsumptionContext::getRebirthChainUsed(BattleSide side) const
{
	return battle.getRebirthChainUsed(side);
}
const newHorizonsCreatures::CreatureCategoryRules & LiveElementalConsumptionContext::getCreatureCategoryRules() const
{
	return battle.getCreatureCategoryRules();
}
std::optional<CreatureID> LiveElementalConsumptionContext::primaryElemental() const
{
	return newHorizonsElementalTerrain::primaryElemental(battle);
}
int32_t LiveElementalConsumptionContext::maximumSummonHealth(BattleSide side, CreatureID type)
{
	return effectiveSummonMaxHP(battle.getSideArmy(side), type, battle.getSidePlayer(side), side);
}
