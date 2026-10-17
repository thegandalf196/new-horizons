/*
 * LiveUnitEnvironmentContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "LiveUnitEnvironmentContext.h"
#include "CBattleInfoCallback.h"
#include "BattleInfo.h"
#include "NewHorizonsDiscipline.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../entities/artifact/CArtifact.h"

bool battle::heroHasAmmoCartArtifact(const CGHeroInstance * hero)
{
	if(!hero)
		return false;
	const auto artifact = hero->artifactsWorn.find(ArtifactPosition::MACH2);
	return artifact != hero->artifactsWorn.end()
		&& artifact->second.getArt()->getTypeId() == ArtifactID::AMMO_CART;
}

battle::LiveUnitEnvironmentContext::LiveUnitEnvironmentContext(const CBattleInfoCallback & callback)
	: callback(callback), owners(callback) {}
bool battle::LiveUnitEnvironmentContext::available() const { return callback.getBattle() != nullptr; }
const battle::UnitOwnershipQuery & battle::LiveUnitEnvironmentContext::ownership() const { return owners; }
battle::Units battle::LiveUnitEnvironmentContext::unitsIf(const UnitFilter & predicate) const { return callback.battleGetUnitsIf(predicate); }
bool battle::LiveUnitEnvironmentContext::sideHasAmmoArtifact(BattleSide side) const
{
	return heroHasAmmoCartArtifact(callback.getBattle()->getSideHero(side));
}
bool battle::LiveUnitEnvironmentContext::sideHasVeteranCohesion(BattleSide side) const
{
	return newHorizonsDiscipline::hasVeteranCohesion(callback.battleGetFightingHero(side));
}
SylvanLuckState battle::LiveUnitEnvironmentContext::fortune(BattleSide side) const { return callback.getBattle()->getSylvanLuckState(side); }
int battle::LiveUnitEnvironmentContext::bloodrageDamage(BattleSide side) const { return callback.getBattle()->getBloodrageDamagePercent(side); }
int battle::LiveUnitEnvironmentContext::bloodrageRank(BattleSide side) const { return callback.getBattle()->getBloodrageRank(side); }
int battle::LiveUnitEnvironmentContext::bloodrageCap(BattleSide side) const { return callback.getBattle()->getBloodrageCapPercent(side); }
int battle::LiveUnitEnvironmentContext::bloodrageSpeed(BattleSide side) const { return callback.getBattle()->getBloodrageSpeedBonus(side); }
int battle::LiveUnitEnvironmentContext::bloodrageRetaliations(BattleSide side) const { return callback.getBattle()->getBloodrageAdditionalRetaliations(side); }
int battle::LiveUnitEnvironmentContext::bloodragePain(BattleSide side) const { return callback.getBattle()->getBloodragePainIncrement(side); }
int battle::LiveUnitEnvironmentContext::magicResistance(const Unit * unit) const { return callback.battleGetMagicResistance(unit); }
const JsonNode & battle::LiveUnitEnvironmentContext::magicRules() const { return callback.getBattle()->getMagicRules(); }

bool CBattleInfoCallback::battleUnitHasAmmoCart(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitHasAmmoCart(unit);
}
int CBattleInfoCallback::battleFortuneSpeed(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitFortuneSpeed(unit);
}
int CBattleInfoCallback::battleBloodrageSpeed(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitSpeedBonus(unit);
}
int CBattleInfoCallback::battleBloodrageRetaliations(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitAdditionalRetaliations(unit);
}
int CBattleInfoCallback::battleBloodragePainIncrement(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitBloodragePainIncrement(unit);
}
int CBattleInfoCallback::battleGetBloodrageDamagePercent(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).bloodrageDamagePercent(unit);
}
bool CStack::unitHasVeteranCohesion(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return false;
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitHasVeteranCohesion(unit);
}
std::optional<std::pair<int32_t, int32_t>> CStack::unitMoraleLimits(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return std::nullopt;
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitMoraleLimits(unit);
}
