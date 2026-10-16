/*
 * BattleEvaluatorHeroQueue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "BattleEvaluatorProjection.h"
#include "HypotheticBattle.h"
#include "PotentialTargets.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../lib/battle/NewHorizonsSeizeInitiative.h"
#include "../../lib/bonuses/BonusSelector.h"

#include <algorithm>
#include <iterator>

void expireCompletedProjectedBerserkActivation(HypotheticBattle & projectedBattle,
	const battle::Unit * unit)
{
	// The caller invokes this only after a forced Berserk action was projected.
	// Unlike a successful attack, WALK and NO_ACTION do not consume UNTIL_OWN_ATTACK,
	// so remove only the shared helper's eligible saved-v3 Berserk spell markers.
	const auto bonuses = newHorizonsBerserk::completedForcedActivationBonuses(projectedBattle, unit);
	if(unit && !bonuses.empty())
		projectedBattle.removeUnitBonus(unit->unitId(), bonuses);
}

bool BattleEvaluator::evaluateHeroActionQueue(PossibleSpellcast::ValueMap & values,
	const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state,
	size_t minTurnSpan, bool * enemyHadTurnOut)
{
	bool firstRound = true;
	bool enemyHadTurn = false;
	size_t ourTurnSpan = 0;

	bool stop = false;

	for(auto & round : queue)
	{
		if(!firstRound)
			state->nextRound();
		auto remainingQueue = round;
		for(size_t queueIndex = 0; queueIndex < remainingQueue.size(); ++queueIndex)
		{
			const auto * queuedUnit = remainingQueue[queueIndex];
			const auto * unit = state->battleGetUnitByID(queuedUnit->unitId());
			if(!unit)
				continue;
			if(!vstd::contains(values, unit->unitId()))
				values[unit->unitId()] = 0;

			if(!unit->alive())
				continue;

			if(state->battleGetActionController(unit) != playerID)
			{
				enemyHadTurn = true;

				const auto enemySide = state->playerToSide(state->battleGetActionController(unit));
				const auto & enemyAllowances = state->getHeroActionAllowances(enemySide);
				const bool enemyCanPayForSpell = enemyAllowances.currentRound >= 0
					? enemyAllowances.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL,
						state->getRound()).has_value()
					: state->battleCastSpells(enemySide) == 0;
				if(!firstRound || enemyCanPayForSpell)
				{
					//enemy could counter our spell at this point
					//anyway, we do not know what enemy will do
					//just stop evaluation
					stop = true;
					break;
				}
			}
			else if(!enemyHadTurn)
			{
				ourTurnSpan++;
			}

			projectHeroActionQueueActivation(values, state, unit, remainingQueue, queueIndex);

		}

		firstRound = false;

		if(stop)
			break;
	}

	if(enemyHadTurnOut)
		*enemyHadTurnOut = enemyHadTurn;

	return ourTurnSpan >= minTurnSpan;
}

void BattleEvaluator::projectHeroActionQueueActivation(PossibleSpellcast::ValueMap & values,
	const std::shared_ptr<HypotheticBattle> & state, const battle::Unit * unit,
	battle::Units & remainingQueue, size_t queueIndex)
{
	const bool frozenForfeits = newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
	const bool syntheticStoppedSlot = unit->isTimeStopped();
	if(frozenForfeits)
		state->nextTurnPreservingFrozenForfeit(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	else
		state->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	if(frozenForfeits)
	{
		const auto frozen = newHorizonsFrozen::removalPlan(*unit);
		state->getForUpdate(unit->unitId())->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
		state->getForUpdate(unit->unitId())->consumeNoQuarterActivation();
		newHorizonsPlague::completeActivation(*state, *state->getServerCallback(), unit->unitId());
		const auto current = state->getForUpdate(unit->unitId());
		if(current->alive() && !current->isGhost() && !current->isTimeStopped()
			&& newHorizonsFrozen::isFrozen(*current))
			current->removeUnitBonus(frozen);
		state->completeSeizeInitiativeActivation(unit->unitId());
		battle::Units remaining(remainingQueue.begin() + queueIndex + 1, remainingQueue.end());
		newHorizonsSeizeInitiative::reorder(*state, remaining, false);
		std::copy(remaining.begin(), remaining.end(), remainingQueue.begin() + queueIndex + 1);
		return;
	}

	PotentialTargets potentialTargets(unit, damageCache, state);

	if(!potentialTargets.possibleAttacks.empty())
	{
		AttackPossibility attackPossibility = potentialTargets.bestAction();

		auto stackWithBonuses = state->getForUpdate(unit->unitId());
		const bool attackerWasAlive = stackWithBonuses->alive();
		*stackWithBonuses = *attackPossibility.attackerState;
		state->recordBloodrageTransition(stackWithBonuses, attackerWasAlive);

		if(attackPossibility.defenderDamageReduce > 0)
		{
			stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
			stackWithBonuses->removeUnitBonus(Bonus::UntilOwnAttack);
		}
		if(attackPossibility.attackerDamageReduce > 0)
			stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);

		for(auto affected : attackPossibility.affectedUnits)
		{
			stackWithBonuses = state->getForUpdate(affected->unitId());
			const bool affectedWasAlive = stackWithBonuses->alive();
			*stackWithBonuses = *affected;
			state->recordBloodrageTransition(stackWithBonuses, affectedWasAlive);

			if(attackPossibility.defenderDamageReduce > 0)
				stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);
			if(attackPossibility.attackerDamageReduce > 0 && attackPossibility.attack.defender->unitId() == affected->unitId())
				stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
		}
	}

	float bav = potentialTargets.berserk
		? potentialTargets.expectedBerserkActionValue()
		: static_cast<float>(potentialTargets.bestActionValue());

	//best action is from effective owner`s point if view, we need to convert to our point if view
	if(state->battleGetActionController(unit) != playerID)
		bav = -bav;
	values[unit->unitId()] += bav;
	if(potentialTargets.berserk)
		expireCompletedProjectedBerserkActivation(*state,
			state->battleGetUnitByID(unit->unitId()));
	if(!syntheticStoppedSlot)
		state->getForUpdate(unit->unitId())->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
	// No-target movement is not a modeled completed action here.
	const auto completed = state->getForUpdate(unit->unitId());
	if(!potentialTargets.possibleAttacks.empty() && !syntheticStoppedSlot
		&& !completed->isTimeStopped() && completed->pursuitMovementRemaining == 0
		&& !state->battleHasPendingRangedFollowUp(completed.get()))
		newHorizonsPlague::completeActivation(*state, *state->getServerCallback(), unit->unitId());
	state->completeSwiftNormalActivation(unit->unitId());
	newHorizonsDivineMandate::completeDisciplineActivation(*state, unit->unitId());
	state->completeSeizeInitiativeActivation(unit->unitId());
	if(state->getSeizeInitiativeState().enabled())
	{
		battle::Units remaining(remainingQueue.begin() + queueIndex + 1, remainingQueue.end());
		newHorizonsSeizeInitiative::reorder(*state, remaining, false);
		std::copy(remaining.begin(), remaining.end(), remainingQueue.begin() + queueIndex + 1);
	}
	if(state->getRapidResponseState(BattleSide::ATTACKER).enabled
		|| state->getRapidResponseState(BattleSide::DEFENDER).enabled)
	{
		// Completed ordinary actions only: move an existing remaining
		// slot without manufacturing or simulating an earned extra chain.
		auto completed = state->getForUpdate(unit->unitId());
		completed->movedThisRound = true;
		completed->waiting = false;
		state->resetActiveUnit();
		state->completeRapidResponseActivation(unit->unitId(), syntheticStoppedSlot);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto * pending = newHorizonsRapidResponse::pendingWaiter(*state, side);
			if(!pending)
				continue;
			const auto begin = remainingQueue.begin() + queueIndex + 1;
			const auto found = std::find_if(begin, remainingQueue.end(), [pending](const auto * queued)
			{
				return queued->unitId() == pending->unitId();
			});
			if(found != remainingQueue.end())
				std::rotate(begin, found, std::next(found));
		}
	}
}
