/*
 * UnitEnvironmentQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitEnvironmentQueries.h"
#include "UnitOwnershipQuery.h"
#include "Unit.h"
#include "NewHorizonsBloodrage.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSorceryRevision.h"
#include <algorithm>
#include <stdexcept>

battle::UnitEnvironmentQueries::UnitEnvironmentQueries(const UnitEnvironmentContext & context) : context(context) {}

bool battle::UnitEnvironmentQueries::unitHasAmmoCart(const Unit * unit) const
{
	if(!context.available() || !unit)
		return false;
	context.requireSubject(*unit);
	const auto carts = context.unitsIf([this, unit](const Unit * candidate)
	{
		return candidate->isAmmoCart() && context.ownership().matches(*candidate, *unit, true);
	});
	// Preserve first matching cart, including a dead cart; never sort/filter alive.
	if(!carts.empty())
		return carts.front()->alive();
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side == BattleSide::NONE)
		return false;
	return context.sideHasAmmoArtifact(side);
}

PlayerColor battle::UnitEnvironmentQueries::unitEffectiveOwner(const Unit * unit) const
{
	if(!context.available())
		return PlayerColor::CANNOT_DETERMINE;
	if(!unit)
		throw std::invalid_argument("Unit ownership requires a subject");
	context.requireSubject(*unit);
	return context.ownership().owner(*unit);
}

std::optional<int> battle::UnitEnvironmentQueries::unitMagicResistance(const Unit * unit) const
{
	if(!context.available() || !unit)
		return std::nullopt;
	context.requireSubject(*unit);
	return context.magicResistance(unit);
}

int battle::UnitEnvironmentQueries::unitFortuneSpeed(const Unit * unit) const
{
	if(!unit || !context.available())
		return 0;
	context.requireSubject(*unit);
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? context.fortune(side).speedBonus(unit->unitId()) : 0;
}

int battle::UnitEnvironmentQueries::bloodrageDamagePercent(const Unit * unit) const
{
	if(!context.available() || !unit)
		return 0;
	context.requireSubject(*unit);
	if(!unit->alive() || unit->isGhost())
		return 0;
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = std::max(0, context.bloodrageCap(side));
	if(cap == 0)
		return 0;
	const int globalDamage = std::max(0, context.bloodrageDamage(side));
	const int personalIncrement = std::max(0, unit->getPersonalBloodrageIncrement());
	return static_cast<int>(std::min<int64_t>(cap, static_cast<int64_t>(globalDamage) + personalIncrement));
}

bool battle::UnitEnvironmentQueries::unitSlowAffectsSpeed(const Unit * unit) const
{
	if(!context.available() || !unit)
		return false;
	context.requireSubject(*unit);
	return newHorizonsSorceryRevision::slowAffectsSpeed(context.magicRules());
}

int battle::UnitEnvironmentQueries::unitSpeedBonus(const Unit * unit) const
{
	if(!context.available() || !unit)
		return 0;
	context.requireSubject(*unit);
	if(!unit->alive() || unit->isGhost())
		return 0;
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = context.bloodrageCap(side);
	const int damage = bloodrageDamagePercent(unit);
	const int bonus = std::max(0, context.bloodrageSpeed(side));
	return cap > 0 && static_cast<int64_t>(damage) * 2 >= cap ? bonus : 0;
}

int battle::UnitEnvironmentQueries::unitAdditionalRetaliations(const Unit * unit) const
{
	if(!context.available() || !unit)
		return 0;
	context.requireSubject(*unit);
	if(!unit->alive() || unit->isGhost())
		return 0;
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int cap = context.bloodrageCap(side);
	const int damage = bloodrageDamagePercent(unit);
	const int bonus = std::max(0, context.bloodrageRetaliations(side));
	return cap > 0 && static_cast<int64_t>(damage) * 2 >= cap ? bonus : 0;
}

int battle::UnitEnvironmentQueries::unitBloodragePainIncrement(const Unit * unit) const
{
	if(!context.available() || !unit)
		return 0;
	context.requireSubject(*unit);
	if(!unit->alive() || unit->isGhost())
		return 0;
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const int rank = context.bloodrageRank(side);
	const int increment = context.bloodragePain(side);
	return context.bloodrageCap(side) > 0 && rank > 0
		&& increment == newHorizonsBloodrage::incrementForRank(rank) ? increment : 0;
}

bool battle::UnitEnvironmentQueries::unitHasVeteranCohesion(const Unit * unit) const
{
	if(!context.available() || !unit)
		return false;
	context.requireSubject(*unit);
	const auto owner = context.ownership().owner(*unit);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(context.ownership().sidePlayer(side) == owner)
			return context.sideHasVeteranCohesion(side);
	return false;
}

std::optional<std::pair<int32_t, int32_t>> battle::UnitEnvironmentQueries::unitMoraleLimits(const Unit * unit) const
{
	if(!context.available() || !unit)
		return std::nullopt;
	context.requireSubject(*unit);
	return newHorizonsMagic::moraleLimits(context.magicRules());
}

newHorizonsNecromancy::CombatPassives battle::UnitEnvironmentQueries::unitNecromancyPassives(const Unit * unit) const
{
	if(!context.available() || !unit)
		return {};
	context.requireSubject(*unit);
	const auto side = context.ownership().playerToSide(context.ownership().owner(*unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	return newHorizonsNecromancy::passivesForCreature(context.necromancyPassives(side), unit->creatureId());
}
