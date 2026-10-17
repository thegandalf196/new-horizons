/*
 * NewHorizonsCrisisCommand.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "NewHorizonsCrisisCommand.h"
#include "CrisisCommandReferences.h"
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
	// Continuation retains its pointer-free wire comparison, independently of
	// exact storage equality; include every optional spell/Order payload field.
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
	validateCrisisState(*this, LiveCrisisReferences(battle), replacementSide, replacementLedger);
}

void State::validateSerializedReferences(const IBattleInfo & battle, BattleSide replacementSide,
	const HeroActionAllowanceState * replacementLedger) const
{
	validateCrisisSerializedReferences(*this, LiveCrisisReferences(battle), replacementSide, replacementLedger);
}
}
