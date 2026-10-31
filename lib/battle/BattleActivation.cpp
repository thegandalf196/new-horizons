/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsPrismaticVanguard.h"
#include "NewHorizonsHeroicSpirit.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsOffense.h"
#include "NewHorizonsBattlecraft.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/BonusSelector.h"
void BattleInfo::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
	retireRelinquishedDreadCavalry(unitId, reason);
	activeStack = unitId;
	if(reason == BattleUnitTurnReason::CRISIS_ORDER || reason == BattleUnitTurnReason::CRISIS_RESUME)
		return;
	if(reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
		|| reason == BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION)
		return;

	CStack * st = getStack(activeStack);
	if(battleBeginsActivation(st, reason))
	{
		const bool sylvanRedeploy = st->defended() && !st->armorerLastStandDefending;
		newHorizonsSerpentsEmbrace::releaseBindings(*this, unitId);
		st->moraleExtraActivation = moraleExtraDamagePercent != 100 && reason == BattleUnitTurnReason::MORALE;
		newHorizonsHeroicSpirit::beginActivation(*st, reason);
		const auto vanguard = newHorizonsPrismaticVanguard::activationPlan(*this, *st, reason);
		if(!vanguard.remove.empty()) st->removeBonuses(CSelector(newHorizonsPrismaticVanguard::isEffect));
		if(!vanguard.add.empty()) addUnitBonus(unitId, vanguard.add);
		st->removeBonusesRecursive(CSelector(Bonus::UntilNextCreatureActivation));

		// Second Wind is a genuine activation too, but must not broaden the
		// lifetime of unrelated legacy STACK_GETS_TURN bonuses.
		st->removeBonusesRecursive(CSelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));
		const auto owner = playerToSide(battleGetOwner(st));
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			sides.at(side).sylvanLuck.beginActivation(unitId, side == owner, sylvanRedeploy, st->waited());
	}
	if(st->isTimeStopped() && reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		// A stopped unit receives a synthetic queue activation solely so the
		// authoritative flow can reach a Hero Action window. Mark that queue
		// slot consumed without changing movement/action permission; the flag is
		// reset by afterNewRound and is used only by turn ordering.
		st->timeStopTurnConsumedFlag = true;
	}

	// A hero/creature spell that does not consume the active unit's turn is a
	// continuation of the same activation.  Keep the Fire Wall activation token
	// stable across those transitions, otherwise a repeated movement callback
	// could deal the same wall damage twice.  HERO_COMMAND is normally another
	// continuation, except for the explicit Second Wind activation whose state
	// is armed before the transition is published.
	bool newActivation = reason != BattleUnitTurnReason::HERO_SPELLCAST
		&& reason != BattleUnitTurnReason::UNIT_SPELLCAST;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		newActivation = false;
		if(st)
		{
			const auto controllerSide = playerToSide(battleGetOwner(st));
			if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
			{
				const auto * orderState = sides.at(controllerSide).findOrder(HeroCommand::SECOND_WIND);
				newActivation = orderState
					&& orderState->secondWindActive
					&& orderState->primaryTargetUnitId == unitId;
			}
		}
	}
	if(newActivation)
	{
		beginActivationActionState(*st, unitId, reason);
	}
	if(newActivation && activationSerial < std::numeric_limits<si32>::max())
		++activationSerial;

	if (reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
	{
		//remove bonuses that last until when stack gets new turn
		if(!st->isTimeStopped())
			st->removeBonusesRecursive(CSelector([](const Bonus * bonus)
			{
				return Bonus::UntilGetsTurn(bonus)
					&& !newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
			}));
	}

	if(battleBeginsActivation(st, reason))
		st->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
			battleGetOwnerHero(st), st, reason));

	st->afterGetsTurn(reason);
}
