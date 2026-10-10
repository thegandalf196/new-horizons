/*
 * NewHorizonsSeizeInitiative.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsSeizeInitiative.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsFrozen.h"
#include "../mapObjects/CGHeroInstance.h"

void SeizeInitiativeState::validateShape() const
{
	if(round < -1 || (activeNormal && active == NO_UNIT)
		|| !std::is_sorted(normalCompleted.begin(), normalCompleted.end())
		|| std::adjacent_find(normalCompleted.begin(), normalCompleted.end()) != normalCompleted.end()
		|| std::binary_search(normalCompleted.begin(), normalCompleted.end(), NO_UNIT))
		throw std::runtime_error("Malformed Seize Initiative normal activation ledger");
	for(const auto & receipt : sides)
		if((!receipt.enabled && receipt != Receipt{})
		|| (!receipt.triggered && (receipt.recipient != NO_UNIT || receipt.awaitingAnchor || receipt.anchor != NO_UNIT))
			|| (receipt.awaitingAnchor && (receipt.anchor == NO_UNIT || receipt.recipient == NO_UNIT))
			|| (!receipt.awaitingAnchor && receipt.anchor != NO_UNIT))
			throw std::runtime_error("Malformed Seize Initiative receipt");
	if(!enabled() && (round != -1 || active != NO_UNIT || !normalCompleted.empty()))
		throw std::runtime_error("Inactive Seize Initiative carries activation state");
	if(enabled() && round < 0)
		throw std::runtime_error("Seize Initiative lacks captured round");
	if(activeNormal && completed(active))
		throw std::runtime_error("Seize Initiative normal slot already completed");
}

void SeizeInitiativeState::nextRound(int32_t next)
{
	if(!enabled())
		return;
	round = next;
	active = NO_UNIT;
	activeNormal = false;
	normalCompleted.clear();
	for(auto & receipt : sides)
	{
		receipt.recipient = NO_UNIT;
		receipt.anchor = NO_UNIT;
		receipt.awaitingAnchor = false;
	}
}

void SeizeInitiativeState::begin(uint32_t id, bool normal)
{
	if(!enabled())
		return;
	active = id;
	activeNormal = normal && !completed(id);
	for(auto & receipt : sides)
		if(!receipt.awaitingAnchor && receipt.recipient == id && activeNormal)
			receipt.recipient = NO_UNIT;
}

void SeizeInitiativeState::complete(uint32_t id)
{
	if(!enabled() || active != id)
		return;
	if(activeNormal && !completed(id))
		normalCompleted.insert(std::lower_bound(normalCompleted.begin(), normalCompleted.end(), id), id);
	active = NO_UNIT;
	activeNormal = false;
	for(auto & receipt : sides)
		if(receipt.awaitingAnchor && receipt.anchor == id)
		{
			receipt.awaitingAnchor = false;
			receipt.anchor = NO_UNIT;
		}
}

namespace newHorizonsSeizeInitiative
{
bool eligible(const CBattleInfoCallback & cb, BattleSide side, const battle::Unit * unit)
{
	if(!cb.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !unit || !unit->alive() || unit->isGhost() || unit->isTimeStopped()
		|| newHorizonsFrozen::isFrozen(*unit) || !unit->willMove()
		|| cb.battleGetOwner(unit) != cb.getBattle()->getSidePlayer(side))
		return false;
	const auto & state = cb.getBattle()->getSeizeInitiativeState();
	return state.sides.at(static_cast<size_t>(side)).enabled
		&& state.round == cb.battleGetRound() && !state.completed(unit->unitId());
}

SeizeInitiativeState capturePaidOrder(const CBattleInfoCallback & cb, BattleSide side)
{
	auto state = cb.getBattle()->getSeizeInitiativeState();
	auto & receipt = state.sides.at(static_cast<size_t>(side));
	if(!receipt.enabled || receipt.triggered)
		return state;
	receipt.triggered = true;
	std::vector<battle::Units> turns;
	cb.battleGetTurnOrder(turns, 0, 1, 0, BattleSide::NONE, false, false);
	if(turns.empty())
		return state;
	const auto * active = cb.battleActiveUnit();
	for(auto it = turns.front().rbegin(); it != turns.front().rend(); ++it)
		if((!active || (*it)->unitId() != active->unitId()) && eligible(cb, side, *it))
		{
			receipt.recipient = (*it)->unitId();
			if(active)
			{
				receipt.anchor = active->unitId();
				receipt.awaitingAnchor = true;
			}
			break;
		}
	return state;
}

void reorder(const CBattleInfoCallback & cb, battle::Units & queue, bool protectActive)
{
	const auto & state = cb.getBattle()->getSeizeInitiativeState();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & receipt = state.sides.at(static_cast<size_t>(side));
		if(receipt.awaitingAnchor || receipt.recipient == SeizeInitiativeState::NO_UNIT)
			continue;
		const auto chosen = std::find_if(queue.begin(), queue.end(), [&](const auto * unit)
		{
			return unit->unitId() == receipt.recipient && eligible(cb, side, unit);
		});
		if(chosen == queue.end())
			continue;
		// Leave enemy positions untouched, and protect an already active extra.
		std::vector<size_t> positions;
		for(size_t index = 0; index < queue.size(); ++index)
			if(!(protectActive && index == 0 && queue[index] == cb.battleActiveUnit())
				&& cb.battleGetOwner(queue[index]) == cb.getBattle()->getSidePlayer(side))
				positions.push_back(index);
		const auto chosenIndex = static_cast<size_t>(chosen - queue.begin());
		const auto last = std::find(positions.begin(), positions.end(), chosenIndex);
		if(last == positions.end())
			continue;
		const auto * recipient = *chosen;
		for(auto it = last; it != positions.begin(); --it)
			queue[*it] = queue[*std::prev(it)];
		queue[positions.front()] = recipient;
	}
}

void validateReferences(const CBattleInfoCallback & cb, const SeizeInitiativeState & state)
{
	state.validateShape();
	if(!state.enabled())
		return;
	if(state.round != cb.battleGetRound())
		throw std::runtime_error("Seize Initiative round mismatch");
	const auto units = cb.battleGetUnitsIf([](const auto *) { return true; });
	const auto present = [&](uint32_t id)
	{
		return id == SeizeInitiativeState::NO_UNIT || std::count_if(units.begin(), units.end(), [id](const auto * unit)
			{ return unit && unit->unitId() == id; }) == 1;
	};
	if(!present(state.active))
		throw std::runtime_error("Seize Initiative active reference missing or ambiguous");
	if(state.active != SeizeInitiativeState::NO_UNIT
		&& static_cast<int64_t>(state.active) != cb.getBattle()->getActiveStackID())
	{
		const auto & crisis = cb.getBattle()->getCrisisCommandState();
		const bool suspended = !crisis.returns.empty()
			&& (crisis.returns.back().phase == newHorizonsCrisisCommand::Phase::CHOICE
				|| crisis.returns.back().phase == newHorizonsCrisisCommand::Phase::GRANTED_EXTRA)
			&& crisis.returns.back().round == state.round
			&& crisis.returns.back().suspendedSeizeActive == state.active
			&& crisis.returns.back().suspendedSeizeActiveNormal == state.activeNormal
			&& static_cast<int64_t>(crisis.returns.back().anchor) == cb.getBattle()->getActiveStackID();
		if(!suspended)
			throw std::runtime_error("Seize Initiative active ledger does not match battle anchor");
	}
	for(auto id : state.normalCompleted)
		if(!present(id))
			throw std::runtime_error("Seize Initiative completed reference missing or ambiguous");
	for(const auto & receipt : state.sides)
		if(!present(receipt.recipient) || !present(receipt.anchor))
			throw std::runtime_error("Seize Initiative recipient reference missing or ambiguous");
}
}
