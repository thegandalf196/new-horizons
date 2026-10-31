/*
 * BattleInfoUnitUpdate.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "AccessibilityInfo.h"
#include "UnitUpdateAdmission.h"
#include "UnitTransitionSidecars.h"
#include "../bonuses/BonusSelector.h"
#include <stdexcept>

namespace
{
// Only changes to a provider's identity, footprint or presence invalidate peers.
struct AdjacencyProvider
{
	BattleHex position;
	CreatureID creature;
	const CCreature * type;
	bool alive;
	bool ghost;
	bool operator==(const AdjacencyProvider &) const = default;
};

AdjacencyProvider adjacencyProvider(const CStack & unit)
{
	return {unit.getPosition(), unit.creatureId(), unit.unitType(), unit.alive(), unit.isGhost()};
}

void retireKilledUnitsClone(BattleInfo & battle, CStack & unit, bool sourceRetired)
{
	if(unit.cloneID < 0)
		return;
	auto * clone = battle.getStack(unit.cloneID);
	if(clone)
	{
		if(!sourceRetired)
			newHorizonsSerpentsEmbrace::releaseBindings(battle, clone->unitId());
		const auto previous = adjacencyProvider(*clone);
		clone->makeGhost();
		if(adjacencyProvider(*clone) != previous)
			battle.nodeHasChanged();
	}
	unit.cloneID = -1;
}
}

void BattleInfo::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	CStack * changedStack = getStack(id, false);
	if(!changedStack)
		throw std::runtime_error("Invalid unit id in BattleInfo update");
	const auto updateGate = battle::classifyUnitUpdateGate(*changedStack, healthDelta);
	if(updateGate == battle::UnitUpdateGate::IGNORE_HEALTH_STOP)
	{
		logNetwork->warn("Ignoring health update of Time Stop unit %d", id);
		return;
	}
	if(updateGate == battle::UnitUpdateGate::IGNORE_STATE_STOP)
	{
		// UnitChanges also carries a serialized state.  A stopped unit cannot
		// move, wait, defend, cast, or otherwise mutate activation state; the
		// authoritative flow advances it with a no-op instead.  Reject all
		// external state updates until the marker is released.
		logNetwork->warn("Ignoring state update of Time Stop unit %d", id);
		return;
	}

	if(updateGate == battle::UnitUpdateGate::CHECK_RESURRECTION_SPACE)
	{
		//checking if we resurrect a stack that is under a living stack
		auto accessibility = getAccessibility();

		if(!accessibility.accessible(changedStack->getPosition(), changedStack))
		{
			logNetwork->error("Cannot resurrect %s because hex %d is occupied!", changedStack->nodeName(), changedStack->getPosition());
			return; //position is already occupied
		}
	}

	const auto previousProvider = adjacencyProvider(*changedStack);
	bool killed = (-healthDelta) >= changedStack->getAvailableHealth();//todo: check using alive state once rebirth will be handled separately

	bool resurrected = !changedStack->alive() && healthDelta > 0;

	//applying changes
	changedStack->load(data);
	const bool serpentSourceRetired = (killed && healthDelta < 0)
		|| (previousProvider.alive && !changedStack->alive());
	if(serpentSourceRetired)
		newHorizonsSerpentsEmbrace::releaseDeathBindings(*this, *changedStack);
	// Adjacency limiters can depend on this provider from any battle recipient.
	if(adjacencyProvider(*changedStack) != previousProvider)
		nodeHasChanged();
	expireSeparatedHeroOrderProtect();


	if(healthDelta < 0)
	{
		changedStack->removeBonusesRecursive(Bonus::UntilBeingAttacked);
	}

	if(healthDelta < 0)
	{
		changedStack->nodeHasChanged();	//bonuses with TIMES_STACK_SIZE updater may change
	}

	resurrected = resurrected || (killed && changedStack->alive());

	if(killed)
		retireKilledUnitsClone(*this, *changedStack, serpentSourceRetired);

	if(resurrected || killed)
	{
		//removing all spells effects
		changedStack->removeBonusesRecursive(battle::isTransientSpellEffect);
	}

	if(!changedStack->alive() && changedStack->isClone())
	{
		for(auto & s : stacks)
		{
			if(s->cloneID == changedStack->unitId())
				s->cloneID = -1;
		}
	}
	if(!changedStack->alive())
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			battle::clearReducedActivationForUnit(sides.at(side).reducedExtraActivation, id);
		}
	}
}
