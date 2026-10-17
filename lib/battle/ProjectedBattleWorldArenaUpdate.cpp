/*
 * ProjectedBattleWorldArenaUpdate.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "BattleInfo.h"
#include "UnitTransitionSidecars.h"
#include "../bonuses/BonusSelector.h"
#include <limits>
#include <stdexcept>

void battle::ProjectedBattleWorldArena::Data::update(Slot & slot, const JsonNode & data, int64_t healthDelta)
{
	auto * changedStack = slot.state.get();
	bool killed = (-healthDelta) >= changedStack->getAvailableHealth();
	bool resurrected = !changedStack->alive() && healthDelta > 0;
	const auto previousPosition = changedStack->getPosition();
	graph->loadBoundState(*slot.original, *changedStack, data);
	// Mirror the original BattleInfo root event, after load and before expiry.
	if(changedStack->getPosition() != previousPosition)
		graph->nodeHasChanged(source);
	expireProtect();
	if(healthDelta < 0)
	{
		graph->removeBonusesRecursive(*slot.original, Bonus::UntilBeingAttacked);
		// The imported query layer belongs only to this exact recipient. Native
		// originating removal above still owns propagation/descendant semantics.
		graph->removeRecipientOverlaysMatching(*slot.original, Bonus::UntilBeingAttacked);
	}
	if(healthDelta < 0)
		graph->nodeHasChanged(*slot.original);
	resurrected = resurrected || (killed && changedStack->alive());
	if(killed)
	{
		if(changedStack->cloneID >= 0)
		{
			auto * clone = find(changedStack->cloneID, true);
			if(clone)
				clone->state->makeGhost();
			changedStack->cloneID = -1;
		}
	}
	if(resurrected || killed)
	{
		graph->removeBonusesRecursive(*slot.original, isTransientSpellEffect);
		graph->removeRecipientOverlaysMatching(*slot.original, isTransientSpellEffect);
	}
	if(!changedStack->alive() && changedStack->isClone())
	{
		for(auto & s : slots)
		{
			if(s->state->cloneID == changedStack->unitId())
				s->state->cloneID = -1;
		}
	}
	if(!changedStack->alive())
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			clearReducedActivationForUnit(reduced.at(side), changedStack->unitId());
	}
}

battle::ProjectedBattleWorldArena::UpdateDecision battle::ProjectedBattleWorldArena::updateUnit(
	uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	impl->requireUsable();
	try
	{
		const auto decision = classifyUpdate(id, healthDelta);
		if(decision != UpdateDecision::APPLY)
			return decision;
		// Original negation is undefined for INT64_MIN. Private invalid-domain
		// safety only: never reached by a legacy PREPARSE ignore decision.
		if(healthDelta == std::numeric_limits<int64_t>::min())
			throw std::runtime_error("Projected unit update health delta cannot be negated");
		impl->update(*impl->find(id, false), data, healthDelta);
		return UpdateDecision::APPLY;
	}
	catch(...)
	{
		impl->abandon();
		throw;
	}
}
