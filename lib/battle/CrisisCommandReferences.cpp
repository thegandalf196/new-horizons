/*
 * CrisisCommandReferences.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CrisisCommandReferences.h"
#include "IBattleState.h"
#include "NewHorizonsSeizeInitiative.h"
#include <algorithm>

namespace newHorizonsCrisisCommand
{
namespace
{
bool sideValid(BattleSide side) { return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER; }
}

void validateCrisisState(const State & state, const CrisisCommandReferences & battle,
	BattleSide replacementSide, const HeroActionAllowanceState * replacementLedger)
{
	validateCrisisSerializedReferences(state, battle, replacementSide, replacementLedger);
	for(const auto & frame : state.returns)
		if(frame.phase == Phase::CHOICE)
		{
			const auto anchors = battle.getUnitsIf([&frame](const battle::Unit * unit) { return unit->unitId() == frame.anchor; });
			if(!anchors.front()->alive() || anchors.front()->isGhost() || anchors.front()->isTurret())
				throw std::runtime_error("Crisis Command return references a missing or stale unit");
		}
}

void validateCrisisSerializedReferences(const State & state, const CrisisCommandReferences & battle,
	BattleSide replacementSide, const HeroActionAllowanceState * replacementLedger)
{
	state.validateShape();
	std::vector<uint32_t> unitIds;
	for(const auto * unit : battle.getUnitsIf([](const battle::Unit *) { return true; }))
	{
		if(!unit || !unit->unitType() || !sideValid(unit->unitSide())
			|| std::find(unitIds.begin(), unitIds.end(), unit->unitId()) != unitIds.end())
			throw std::runtime_error("Crisis Command requires unique valid unit descriptors");
		unitIds.push_back(unit->unitId());
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(state.used.at(static_cast<size_t>(side)) && !eligible(battle.getSideHero(side)))
			throw std::runtime_error("Crisis Command receipt lacks its captured active perk");
	for(const auto & receipt : state.pending)
		if(battle.getUnitsIf([&receipt](const battle::Unit * unit) { return unit->unitId() == receipt.unit; }).size() != 1)
			throw std::runtime_error("Crisis Command destruction references a missing unit");
	for(const auto & frame : state.returns)
	{
		const auto anchors = battle.getUnitsIf([&frame](const battle::Unit * unit) { return unit->unitId() == frame.anchor; });
		const auto & seize = battle.getSeizeInitiativeState();
		if((frame.suspendedSeizeActive != NO_UNIT && !seize.enabled())
			|| (frame.suspendedSeizeActiveNormal && seize.completed(frame.suspendedSeizeActive)))
			throw std::runtime_error("Crisis Command suspended Seize provenance is invalid");
		if(frame.round != battle.getRound() || anchors.size() != 1
			|| (frame.originalActor >= 0 && battle.getUnitsIf([&frame](const battle::Unit * unit)
				{ return unit->unitId() == static_cast<uint32_t>(frame.originalActor); }).size() != 1))
			throw std::runtime_error("Crisis Command return references a missing or stale unit");
		if(frame.phase == Phase::CHOICE)
		{
			const auto & grants = replacementLedger && replacementSide == frame.responder
				? replacementLedger->grants : battle.getHeroActionAllowances(frame.responder).grants;
			if(std::none_of(grants.begin(), grants.end(), [&frame](const auto & grant)
				{ return grant.id == frame.grant && grant.source == HeroActionAllowanceState::GrantSource::CRISIS_COMMAND
					&& grant.allowance == HeroActionAllowanceState::AllowanceKind::ORDER
					&& grant.grantedRound == frame.round && grant.expiryRound == frame.round; }))
				throw std::runtime_error("Crisis Command choice has no dedicated allowance");
		}
	}
}

}
