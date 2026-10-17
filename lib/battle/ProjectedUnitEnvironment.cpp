/*
 * ProjectedUnitEnvironment.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitEnvironment.h"
#include "ProjectedUnitEnvironmentState.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include <stdexcept>

battle::ProjectedUnitEnvironmentContext::ProjectedUnitEnvironmentContext(const ProjectedBonusRoster & roster,
	const ProjectedBonusGraph & graph, const ProjectedUnitEnvironmentState & values)
	: roster(roster), graph(graph), values(values), resistance(roster, values.magicRules())
{
	requireRoster();
}

bool battle::ProjectedUnitEnvironmentContext::available() const { return true; }
void battle::ProjectedUnitEnvironmentContext::requireRoster() const
{
	for(const auto * unit : roster.orderedUnits())
		graph.requireBoundUnit(*unit);
}
void battle::ProjectedUnitEnvironmentContext::requireSubject(const Unit & unit) const
{
	if(!roster.contains(unit))
		throw std::invalid_argument("Projected environment subject is outside its roster");
	requireRoster(); // nested aura/limiter queries must not use another replaced provider
}
const battle::UnitOwnershipQuery & battle::ProjectedUnitEnvironmentContext::ownership() const { return *this; }
PlayerColor battle::ProjectedUnitEnvironmentContext::sidePlayer(BattleSide side) const { return values.inputs(side).color; }
battle::Units battle::ProjectedUnitEnvironmentContext::unitsIf(const UnitFilter & predicate) const
{
	requireRoster();
	Units result;
	for(const auto * unit : roster.orderedUnits())
	{
		if(predicate(unit))
			result.push_back(unit);
	}
	return result;
}
bool battle::ProjectedUnitEnvironmentContext::sideHasAmmoArtifact(BattleSide side) const { return values.inputs(side).ammoArtifact; }
bool battle::ProjectedUnitEnvironmentContext::sideHasVeteranCohesion(BattleSide side) const { return values.inputs(side).veteranCohesion; }
SylvanLuckState battle::ProjectedUnitEnvironmentContext::fortune(BattleSide side) const { return values.state(side).fortune; }
int battle::ProjectedUnitEnvironmentContext::bloodrageDamage(BattleSide side) const { return values.state(side).bloodrageDamagePercent; }
int battle::ProjectedUnitEnvironmentContext::bloodrageRank(BattleSide side) const { return values.inputs(side).bloodrageRank; }
int battle::ProjectedUnitEnvironmentContext::bloodrageCap(BattleSide side) const { return values.inputs(side).bloodrageCap; }
int battle::ProjectedUnitEnvironmentContext::bloodrageSpeed(BattleSide side) const { return values.inputs(side).bloodrageSpeed; }
int battle::ProjectedUnitEnvironmentContext::bloodrageRetaliations(BattleSide side) const { return values.inputs(side).bloodrageRetaliations; }
int battle::ProjectedUnitEnvironmentContext::bloodragePain(BattleSide side) const { return values.inputs(side).bloodragePain; }
int battle::ProjectedUnitEnvironmentContext::magicResistance(const Unit * unit) const { return resistance.query(unit); }
const JsonNode & battle::ProjectedUnitEnvironmentContext::magicRules() const { return values.magicRules(); }

battle::ProjectedUnitEnvironment::ProjectedUnitEnvironment(const ProjectedBonusRoster & roster,
	const ProjectedBonusGraph & graph, const ProjectedUnitEnvironmentState & values)
	: context(roster, graph, values), queries(context) {}
bool battle::ProjectedUnitEnvironment::unitHasAmmoCart(const Unit * unit) const { return queries.unitHasAmmoCart(unit); }
PlayerColor battle::ProjectedUnitEnvironment::unitEffectiveOwner(const Unit * unit) const { return queries.unitEffectiveOwner(unit); }
std::optional<int> battle::ProjectedUnitEnvironment::unitMagicResistance(const Unit * unit) const { return queries.unitMagicResistance(unit); }
int battle::ProjectedUnitEnvironment::unitFortuneSpeed(const Unit * unit) const { return queries.unitFortuneSpeed(unit); }
int battle::ProjectedUnitEnvironment::unitSpeedBonus(const Unit * unit) const { return queries.unitSpeedBonus(unit); }
int battle::ProjectedUnitEnvironment::unitAdditionalRetaliations(const Unit * unit) const { return queries.unitAdditionalRetaliations(unit); }
int battle::ProjectedUnitEnvironment::unitBloodragePainIncrement(const Unit * unit) const { return queries.unitBloodragePainIncrement(unit); }
bool battle::ProjectedUnitEnvironment::unitHasVeteranCohesion(const Unit * unit) const { return queries.unitHasVeteranCohesion(unit); }
std::optional<std::pair<int32_t, int32_t>> battle::ProjectedUnitEnvironment::unitMoraleLimits(const Unit * unit) const { return queries.unitMoraleLimits(unit); }
