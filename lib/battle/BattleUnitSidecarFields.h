/*
 * BattleUnitSidecarFields.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "BattleInfo.h"
#include <tuple>
#include <utility>

namespace battle
{
class ProjectedBattleWorldArena;

/// Private mechanical storage inventory, shared by capture, comparison and swap.
/// No rules or serialization live here. Unit packet kernels remain the owners
/// of Protect/reduced activation, Bloodrage, Crisis and elemental transitions.
struct BattleUnitSidecarFields
{
	template<typename Owner> static auto writable(Owner & battle)
	{
		auto & attacker = battle.sides[BattleSide::ATTACKER];
		auto & defender = battle.sides[BattleSide::DEFENDER];
		return std::tie(attacker.orderStates, attacker.reducedExtraActivation,
			attacker.rebirthChainUsed, attacker.phoenixSparkUsed, attacker.bloodrageDamagePercent,
			defender.orderStates, defender.reducedExtraActivation,
			defender.rebirthChainUsed, defender.phoenixSparkUsed, defender.bloodrageDamagePercent,
			battle.crisisCommand.used, battle.crisisCommand.pending,
			battle.bloodrageDestroyedUnits, battle.bloodrageFirstBloodUsed);
	}
	template<typename Owner> static auto inputs(Owner & battle)
	{
		auto & attacker = battle.sides[BattleSide::ATTACKER];
		auto & defender = battle.sides[BattleSide::DEFENDER];
		return std::tie(attacker.activeOrder, attacker.heroActionAllowances,
			attacker.bloodrageRank, attacker.bloodrageCapPercent,
			defender.activeOrder, defender.heroActionAllowances,
			defender.bloodrageRank, defender.bloodrageCapPercent,
			battle.crisisCommand.returns, battle.seizeInitiative,
			battle.battleID, battle.round, battle.terrainType, battle.battlefieldType);
	}
	static auto projected(const ProjectedBattleWorldArena & arena);
	static auto projectedInputs(const ProjectedBattleWorldArena & arena);
};

template<typename Tuple> auto copyBattleSidecarTuple(const Tuple & fields)
{
	return std::apply([](const auto &... field) { return std::make_tuple(field...); }, fields);
}
using BattleUnitSidecarValues = decltype(copyBattleSidecarTuple(
	BattleUnitSidecarFields::writable(std::declval<const BattleInfo &>())));
using BattleUnitSidecarInputs = decltype(copyBattleSidecarTuple(
	BattleUnitSidecarFields::inputs(std::declval<const BattleInfo &>())));

struct BattleUnitSidecarOrigin
{
	BattleUnitSidecarValues values;
	BattleUnitSidecarInputs inputs;
	std::optional<CreatureID> primaryElemental;
};
}
