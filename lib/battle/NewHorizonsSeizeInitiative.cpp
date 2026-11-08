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
bool eligible(const CBattleInfoCallback &, BattleSide, const battle::Unit *)
{
	return false;
}

SeizeInitiativeState capturePaidOrder(const CBattleInfoCallback &, BattleSide)
{
	return {};
}

void reorder(const CBattleInfoCallback &, battle::Units &, bool)
{

}

void validateReferences(const CBattleInfoCallback &, const SeizeInitiativeState & state)
{
	state.validateShape();
	if(state.enabled())
		throw std::runtime_error("Seize Initiative is retired in the current rules");
}
}
