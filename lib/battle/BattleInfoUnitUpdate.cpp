/*
 * BattleInfoUnitUpdate.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "AccessibilityInfo.h"
#include "UnitUpdateAdmission.h"
#include "UnitTransitionSidecars.h"
#include "../bonuses/BonusSelector.h"
#include <stdexcept>

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

	bool killed = (-healthDelta) >= changedStack->getAvailableHealth();//todo: check using alive state once rebirth will be handled separately

	bool resurrected = !changedStack->alive() && healthDelta > 0;

	//applying changes
	const auto previousPosition = changedStack->getPosition();
	changedStack->load(data);
	// Placement limiters can depend on this unit from any battle recipient.
	if(changedStack->getPosition() != previousPosition)
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
	{
		if(changedStack->cloneID >= 0)
		{
			//remove clone as well
			CStack * clone = getStack(changedStack->cloneID);
			if(clone)
				clone->makeGhost();

			changedStack->cloneID = -1;
		}
	}

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
