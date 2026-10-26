/*
 * ProjectedUnitEnvironment.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "UnitEnvironmentQueries.h"
#include "UnitOwnershipQuery.h"
#include "ProjectedUnitMagicResistance.h"

class ProjectedBonusGraph;
class ProjectedBonusRoster;
namespace battle
{
class ProjectedUnitEnvironmentState;

class DLL_LINKAGE ProjectedUnitEnvironmentContext final : public UnitEnvironmentContext, public UnitOwnershipQuery
{
	const ProjectedBonusRoster & roster;
	const ProjectedBonusGraph & graph;
	const ProjectedUnitEnvironmentState & values;
	ProjectedUnitMagicResistance resistance;
	void requireRoster() const;
public:
	ProjectedUnitEnvironmentContext(const ProjectedBonusRoster & roster, const ProjectedBonusGraph & graph,
		const ProjectedUnitEnvironmentState & values);
	bool available() const override;
	void requireSubject(const Unit & unit) const override;
	const UnitOwnershipQuery & ownership() const override;
	PlayerColor sidePlayer(BattleSide side) const override;
	Units unitsIf(const UnitFilter & predicate) const override;
	bool sideHasAmmoArtifact(BattleSide side) const override;
	bool sideHasVeteranCohesion(BattleSide side) const override;
	SylvanLuckState fortune(BattleSide side) const override;
	int bloodrageDamage(BattleSide side) const override;
	int bloodrageRank(BattleSide side) const override;
	int bloodrageCap(BattleSide side) const override;
	int bloodrageSpeed(BattleSide side) const override;
	int bloodrageRetaliations(BattleSide side) const override;
	int bloodragePain(BattleSide side) const override;
	int magicResistance(const Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives necromancyPassives(BattleSide side) const override;
	const JsonNode & magicRules() const override;
};

/// Complete nine-callback composition. Caller owns complete ordered roster,
/// exact graph-bound detached units and captured/private side state. All outlive
/// this stable environment under the EXISTING world ownership interval. No live
/// fallback, ambient mode, automatic sidecar transition or lease is created.
class DLL_LINKAGE ProjectedUnitEnvironment final : public IUnitEnvironment
{
	ProjectedUnitEnvironmentContext context;
	UnitEnvironmentQueries queries;
public:
	ProjectedUnitEnvironment(const ProjectedBonusRoster & roster, const ProjectedBonusGraph & graph,
		const ProjectedUnitEnvironmentState & values);
	ProjectedUnitEnvironment(const ProjectedUnitEnvironment &) = delete;
	ProjectedUnitEnvironment & operator=(const ProjectedUnitEnvironment &) = delete;
	ProjectedUnitEnvironment(ProjectedUnitEnvironment &&) = delete;
	ProjectedUnitEnvironment & operator=(ProjectedUnitEnvironment &&) = delete;
	bool unitHasAmmoCart(const Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const Unit * unit) const override;
	std::optional<int> unitMagicResistance(const Unit * unit) const override;
	int unitFortuneSpeed(const Unit * unit) const override;
	int unitSpeedBonus(const Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const Unit * unit) const override;
	int unitAdditionalRetaliations(const Unit * unit) const override;
	int unitBloodragePainIncrement(const Unit * unit) const override;
	bool unitHasVeteranCohesion(const Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const Unit * unit) const override;
};
}
