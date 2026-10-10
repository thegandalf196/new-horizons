/*
 * NewHorizonsCrisisCommand.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "NewHorizonsCrisisCommand.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsSeizeInitiative.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../serializer/CMemorySerializer.h"
#include <algorithm>

namespace newHorizonsCrisisCommand
{
namespace
{
bool sideValid(BattleSide side) { return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER; }
}

bool activeProfile(const JsonNode & rules)
{
	return newHorizonsHeroes::crisisCommandProfileActive(rules);
}

void validateProfileSerialization(const JsonNode & rules, bool supported)
{
	newHorizonsHeroes::validateCrisisCommandProfileSerialization(rules, supported);
}

bool eligible(const CGHeroInstance * hero)
{
	return hero && activeProfile(hero->getPerkState().rules) && hero->hasActivePerk(SKILL, PERK);
}

const battle::Unit * anchor(const CBattleInfoCallback & battle, BattleSide side)
{
	const battle::Unit * result = nullptr;
	for(const auto * unit : battle.battleGetAllStacks())
		if(unit->alive() && !unit->isGhost() && !unit->isTurret()
			&& battle.battleGetOwner(unit) == battle.sideToPlayer(side)
			&& (!result || unit->unitId() < result->unitId()))
			result = unit;
	return result;
}

bool State::capture(BattleSide side, uint32_t unit)
{
	if(!sideValid(side) || unit == NO_UNIT)
		throw std::runtime_error("Invalid Crisis Command destruction receipt");
	if(used.at(static_cast<size_t>(side))) return false;
	used.at(static_cast<size_t>(side)) = true;
	pending.push_back({side, unit});
	validateShape();
	return true;
}

bool ReturnFrame::sameContinuation(const ReturnFrame & other) const
{
	if(std::tie(responder, anchor, grant, round, originalActor, kind, masterGate, pursuit, ranged,
		suspendedSeizeActive, suspendedSeizeActiveNormal)
		!= std::tie(other.responder, other.anchor, other.grant, other.round, other.originalActor,
			other.kind, other.masterGate, other.pursuit, other.ranged,
			other.suspendedSeizeActive, other.suspendedSeizeActiveNormal)) return false;
	// BattleAction has no equality operator. It is a pointer-free wire value;
	// compare the complete payload, including every optional spell/Order field.
	CMemorySerializer left, right;
	auto first = action, second = other.action;
	left.oser & first; right.oser & second;
	return left.extractBuffer() == right.extractBuffer();
}

void State::validateTransitionFrom(const State & previous) const
{
	validateShape(); previous.validateShape();
	if(used != previous.used || returns.size() > previous.returns.size() + 1
		|| previous.returns.size() > returns.size() + 1)
		throw std::runtime_error("Invalid Crisis Command transition");
	const auto retained = std::min(returns.size(), previous.returns.size());
	for(size_t index = 0; index < retained; ++index)
	{
		if(!returns[index].sameContinuation(previous.returns[index]))
			throw std::runtime_error("Crisis Command altered a suspended action");
		if(returns[index].phase != previous.returns[index].phase
			&& !(index + 1 == returns.size() && returns.size() == previous.returns.size()
				&& previous.returns[index].phase == Phase::CHOICE && returns[index].phase == Phase::GRANTED_EXTRA))
			throw std::runtime_error("Invalid Crisis Command phase transition");
	}
	if(returns.size() > previous.returns.size() || pending != previous.pending)
	{
		if(previous.pending.empty() || pending != std::vector<DeathReceipt>(previous.pending.begin() + 1, previous.pending.end())
			|| (returns.size() > previous.returns.size()
				&& (returns.back().responder != previous.pending.front().side || returns.back().phase != Phase::CHOICE)))
			throw std::runtime_error("Crisis Command altered destruction order");
	}
}

SeizeInitiativeState State::seizeContextAfterTransition(const State & previous,
	const SeizeInitiativeState & current) const
{
	auto next = current;
	if(returns.size() > previous.returns.size())
	{
		const auto & frame = returns.back();
		if(frame.suspendedSeizeActive != current.active
			|| frame.suspendedSeizeActiveNormal != current.activeNormal)
			throw std::runtime_error("Crisis Command captured stale Seize activation provenance");
	}
	else if(returns.size() < previous.returns.size())
	{
		const auto & frame = previous.returns.back();
		next.active = frame.suspendedSeizeActive;
		next.activeNormal = frame.suspendedSeizeActiveNormal;
	}
	next.validateShape();
	return next;
}

void State::validateShape() const
{
	if(pending.size() > 2 || returns.size() > 2)
		throw std::runtime_error("Invalid Crisis Command nesting");
	std::array<bool, 2> assigned{};
	for(const auto & receipt : pending)
	{
		if(!sideValid(receipt.side) || receipt.unit == NO_UNIT
			|| !used.at(static_cast<size_t>(receipt.side)) || assigned.at(static_cast<size_t>(receipt.side)))
			throw std::runtime_error("Invalid Crisis Command pending receipt");
		assigned.at(static_cast<size_t>(receipt.side)) = true;
	}
	for(size_t index = 0; index < returns.size(); ++index)
	{
		const auto & frame = returns[index];
		if(!sideValid(frame.responder) || !used.at(static_cast<size_t>(frame.responder))
			|| assigned.at(static_cast<size_t>(frame.responder)) || frame.anchor == NO_UNIT
			|| frame.grant == 0 || frame.round < 1 || frame.originalActor < -1
			|| (frame.suspendedSeizeActiveNormal && frame.suspendedSeizeActive == NO_UNIT)
			|| (frame.suspendedSeizeActive != NO_UNIT
				&& (frame.originalActor < 0 || frame.suspendedSeizeActive != static_cast<uint32_t>(frame.originalActor)))
			|| (frame.phase != Phase::CHOICE && frame.phase != Phase::GRANTED_EXTRA)
			|| (frame.kind != ReturnKind::ACTION && frame.kind != ReturnKind::NEXT_STACK)
			|| (frame.kind == ReturnKind::ACTION && (!sideValid(frame.action.side)
				|| frame.action.isBattleEndAction() || frame.action.actionType == EActionType::END_TACTIC_PHASE
				|| (frame.action.isUnitAction() && (frame.originalActor < 0
					|| frame.action.stackNumber != static_cast<uint32_t>(frame.originalActor)))))
			|| (index + 1 < returns.size() && frame.phase != Phase::GRANTED_EXTRA)
			|| (frame.kind == ReturnKind::NEXT_STACK && (frame.masterGate || frame.pursuit || frame.ranged)))
			throw std::runtime_error("Invalid Crisis Command return frame");
		assigned.at(static_cast<size_t>(frame.responder)) = true;
	}
}

void State::validate(const IBattleInfo & battle, BattleSide replacementSide,
	const HeroActionAllowanceState * replacementLedger) const
{
	validateSerializedReferences(battle, replacementSide, replacementLedger);
	for(const auto & frame : returns)
		if(frame.phase == Phase::CHOICE)
		{
			const auto anchors = battle.getUnitsIf([&frame](const battle::Unit * unit) { return unit->unitId() == frame.anchor; });
			if(!anchors.front()->alive() || anchors.front()->isGhost() || anchors.front()->isTurret())
				throw std::runtime_error("Crisis Command return references a missing or stale unit");
		}
}

void State::validateSerializedReferences(const IBattleInfo & battle, BattleSide replacementSide,
	const HeroActionAllowanceState * replacementLedger) const
{
	validateShape();
	std::vector<uint32_t> unitIds;
	for(const auto * unit : battle.getUnitsIf([](const battle::Unit *) { return true; }))
	{
		if(!unit || !unit->unitType() || !sideValid(unit->unitSide())
			|| std::find(unitIds.begin(), unitIds.end(), unit->unitId()) != unitIds.end())
			throw std::runtime_error("Crisis Command requires unique valid unit descriptors");
		unitIds.push_back(unit->unitId());
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(used.at(static_cast<size_t>(side)) && !eligible(battle.getSideHero(side)))
			throw std::runtime_error("Crisis Command receipt lacks its captured active perk");
	for(const auto & receipt : pending)
		if(battle.getUnitsIf([&receipt](const battle::Unit * unit) { return unit->unitId() == receipt.unit; }).size() != 1)
			throw std::runtime_error("Crisis Command destruction references a missing unit");
	for(const auto & frame : returns)
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
