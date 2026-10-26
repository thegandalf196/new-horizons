/*
 * ProjectedUnitEnvironmentState.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitEnvironmentState.h"
#include "UnitEnvironmentQueries.h"
#include "IBattleState.h"
#include "NewHorizonsDiscipline.h"

battle::ProjectedUnitEnvironmentState battle::ProjectedUnitEnvironmentState::capture(const IBattleInfo & source)
{
	ProjectedUnitEnvironmentState result;
	result.capturedRules = source.getMagicRules();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & inputs = result.captured.at(side);
		inputs.color = source.getSidePlayer(side);
		const auto * hero = source.getSideHero(side);
		inputs.ammoArtifact = heroHasAmmoCartArtifact(hero);
		inputs.veteranCohesion = newHorizonsDiscipline::hasVeteranCohesion(hero);
		inputs.necromancy = newHorizonsNecromancy::captureCombatPassives(hero);
		inputs.bloodrageRank = source.getBloodrageRank(side);
		inputs.bloodrageCap = source.getBloodrageCapPercent(side);
		inputs.bloodrageSpeed = source.getBloodrageSpeedBonus(side);
		inputs.bloodrageRetaliations = source.getBloodrageAdditionalRetaliations(side);
		inputs.bloodragePain = source.getBloodragePainIncrement(side);
		auto & state = result.states.at(side);
		state.fortune = source.getSylvanLuckState(side);
		state.bloodrageDamagePercent = source.getBloodrageDamagePercent(side);
	}
	return result;
}

const battle::ProjectedUnitEnvironmentState::SideInputs & battle::ProjectedUnitEnvironmentState::inputs(BattleSide side) const { return captured.at(side); }
const battle::ProjectedUnitEnvironmentState::SideState & battle::ProjectedUnitEnvironmentState::state(BattleSide side) const { return states.at(side); }
battle::ProjectedUnitEnvironmentState::SideState & battle::ProjectedUnitEnvironmentState::state(BattleSide side) { return states.at(side); }
const JsonNode & battle::ProjectedUnitEnvironmentState::magicRules() const { return capturedRules; }
