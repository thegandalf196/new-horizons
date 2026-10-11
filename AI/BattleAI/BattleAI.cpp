/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleAI.h"
#include "BattleEvaluator.h"
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "tbb/parallel_for.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CStack.h" // TODO: remove
                              // Eventually only IBattleInfoCallback and battle::Unit should be used,
                              // CUnitState should be private and CStack should be removed completely
#include "../../lib/logging/VisualLogger.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

struct CBattleAI::TimingState
{
	using Clock = std::chrono::steady_clock;
	const BattleID battleID;
	const PlayerColor player;
	const Clock::time_point started = Clock::now();
	std::optional<Clock::time_point> ended;
	std::mutex mutex;
	TimingSummary summary;
	int32_t lastObservedRound = -1;

	TimingState(BattleID battleID, PlayerColor player) : battleID(battleID), player(player) {}

	TimingSummary snapshotLocked() const
	{
		auto result = summary;
		result.battleWallMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
			ended.value_or(Clock::now()) - started).count();
		return result;
	}

	std::optional<TimingSummary> reportLocked()
	{
		if(!summary.finished || summary.activeStackInFlight != 0 || summary.reported)
			return std::nullopt;
		summary.reported = true;
		return snapshotLocked();
	}

	void log(const std::optional<TimingSummary> & result) const
	{
		if(!result)
			return;
		logAi->info("PERFORMANCE: BattleAI callback wall battle=%d player=%s battle_us=%llu calls=%llu total_us=%llu max_us=%llu rounds_observed=%u"
			" evaluator_construct_calls=%llu evaluator_construct_us=%llu stack_select_calls=%llu stack_select_us=%llu"
			" hero_action_including_submit_calls=%llu hero_action_including_submit_us=%llu"
			" direct_unit_submit_calls=%llu direct_unit_submit_us=%llu unclassified_us=%llu",
			battleID.getNum(), player.toString(),
			static_cast<unsigned long long>(result->battleWallMicroseconds),
			static_cast<unsigned long long>(result->activeStackCalls),
			static_cast<unsigned long long>(result->activeStackTotalMicroseconds),
			static_cast<unsigned long long>(result->activeStackMaxMicroseconds), result->roundsObserved,
			static_cast<unsigned long long>(result->evaluatorConstruction.calls),
			static_cast<unsigned long long>(result->evaluatorConstruction.totalMicroseconds),
			static_cast<unsigned long long>(result->stackActionSelection.calls),
			static_cast<unsigned long long>(result->stackActionSelection.totalMicroseconds),
			static_cast<unsigned long long>(result->heroAction.calls),
			static_cast<unsigned long long>(result->heroAction.totalMicroseconds),
			static_cast<unsigned long long>(result->directUnitSubmission.calls),
			static_cast<unsigned long long>(result->directUnitSubmission.totalMicroseconds),
			static_cast<unsigned long long>(result->unclassifiedMicroseconds));
	}
};

class CBattleAI::TimingScope
{
	std::shared_ptr<TimingState> state;
	TimingState::Clock::time_point started = TimingState::Clock::now();
	TimingSummary stageTotals;

	class StageScope
	{
		StageTiming & timing;
		TimingState::Clock::time_point started = TimingState::Clock::now();
	public:
		explicit StageScope(StageTiming & timing) : timing(timing) {}
		~StageScope()
		{
			++timing.calls;
			timing.totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				TimingState::Clock::now() - started).count();
		}
	};
public:
	/// The measured regions are non-nested within this callback. Stage data is
	/// local until callback exit, including when submission destroys the AI owner.
	template<typename Callable>
	decltype(auto) measure(StageTiming TimingSummary::* stage, Callable && action)
	{
		StageScope measurement(stageTotals.*stage);
		return std::forward<Callable>(action)();
	}

	void submitUnitAction(std::shared_ptr<CBattleCallback> callback,
		const BattleID & battleID, const BattleAction & action)
	{
		measure(&TimingSummary::directUnitSubmission, [&]
		{
			callback->battleMakeUnitAction(battleID, action);
		});
	}

	TimingScope(std::shared_ptr<TimingState> current, const BattleID & battleID) : state(std::move(current))
	{
		if(!state || state->battleID != battleID)
		{
			state.reset();
			return;
		}
		bool accepted = false;
		{
			std::lock_guard lock(state->mutex);
			if(!state->summary.finished)
			{
				++state->summary.activeStackCalls;
				++state->summary.activeStackInFlight;
				accepted = true;
			}
		}
		if(!accepted)
			state.reset();
	}

	~TimingScope()
	{
		if(!state)
			return;
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			TimingState::Clock::now() - started).count();
		std::optional<TimingSummary> report;
		{
			std::lock_guard lock(state->mutex);
			uint64_t classifiedMicroseconds = 0;
			for(auto stage : {&TimingSummary::evaluatorConstruction, &TimingSummary::stackActionSelection,
				&TimingSummary::heroAction, &TimingSummary::directUnitSubmission})
			{
				const auto & local = stageTotals.*stage;
				auto & aggregate = state->summary.*stage;
				aggregate.calls += local.calls;
				aggregate.totalMicroseconds += local.totalMicroseconds;
				classifiedMicroseconds += local.totalMicroseconds;
			}
			state->summary.unclassifiedMicroseconds += static_cast<uint64_t>(elapsed) - classifiedMicroseconds;
			state->summary.activeStackTotalMicroseconds += elapsed;
			state->summary.activeStackMaxMicroseconds = std::max(
				state->summary.activeStackMaxMicroseconds, static_cast<uint64_t>(elapsed));
			--state->summary.activeStackInFlight;
			report = state->reportLocked();
		}
		// No CBattleAI pointer: battleEnd may have destroyed the AI during submission.
		state->log(report);
	}
};

std::optional<CBattleAI::TimingSummary> CBattleAI::getTimingSummary() const
{
	const auto current = timingState.load();
	if(!current)
		return std::nullopt;
	std::lock_guard lock(current->mutex);
	return current->snapshotLocked();
}

CBattleAI::CBattleAI()
	: side(BattleSide::NONE),
	wasWaitingForRealize(false)
{
}

CBattleAI::~CBattleAI()
{
	if(cb)
	{
		//Restore previous state of CB - it may be shared with the main AI (like VCAI)
		cb->waitTillRealize = wasWaitingForRealize;
	}
}

void logHexNumbers()
{
#if BATTLE_TRACE_LEVEL >= 1
	logVisual->updateWithLock("hexes", [](IVisualLogBuilder & b)
		{
			for(BattleHex hex = BattleHex(0); hex < GameConstants::BFIELD_SIZE; ++hex)
				b.addText(hex, std::to_string(hex.toInt()));
		});
#endif
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	playerID = *CB->getPlayerID();
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false;
	movesSkippedByDefense = 0;

	logHexNumbers();
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences)
{
	initBattleInterface(ENV, CB);
	autobattlePreferences = autocombatPreferences;
}

BattleAction CBattleAI::useHealingTent(const BattleID & battleID, const CStack *stack)
{
	const auto battle = cb->getBattle(battleID);
	const auto currentControllerSide = battle->playerToSide(battle->battleGetActionController(stack));
	const auto makeActionForUnitSide = [stack](BattleAction action)
	{
		action.side = stack->unitSide();
		return action;
	};
	const auto defend = [&]()
	{
		return makeActionForUnitSide(BattleAction::makeDefend(stack));
	};
	const auto controllerFriendlyStacks = [&]()
	{
		std::vector<const CStack *> result;
		for(const auto * target : battle->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
			if(battle->battleMatchActionController(stack, target, true))
				result.push_back(target);
		return result;
	};
	if(!stack->isFirstAidTent())
	{
		const auto healingTargets = controllerFriendlyStacks();
		std::map<int, const CStack *> woundHpToStack;
		for(const auto * target : healingTargets)
			if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				woundHpToStack[woundHp] = target;
		if(woundHpToStack.empty())
			return defend();
		return makeActionForUnitSide(BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second));
	}

	const auto * tentOwner = battle->battleGetOwnerHero(stack);
	const bool surgeon = tentOwner && tentOwner->hasActivePerk(
		"new-horizons:warMachines", "new-horizons:warMachines.surgeon");
	const bool canonicalSiegeRules = tentOwner
		&& tentOwner->getCapabilityRules()["rulesetVersion"].Integer() >= 3;
	if(!canonicalSiegeRules)
	{
		std::map<int, const CStack *> woundHpToStack;
		for(const auto * target : controllerFriendlyStacks())
			if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				woundHpToStack[woundHp] = target;

		if(surgeon)
		{
			std::map<int, const CStack *> controllerWoundHpToStack;
			std::map<int, const CStack *> afflictionWoundHpToStack;
			for(const auto * target : battle->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
			{
				if(!target->alive() || !target->canBeHealed()
					|| !battle->battleMatchActionController(stack, target, true))
					continue;

				if(const auto woundHp = target->getMaxHealth() - target->getFirstHPleft())
				{
					controllerWoundHpToStack[woundHp] = target;
					if(physicalAfflictions::first(*target))
						afflictionWoundHpToStack[woundHp] = target;
				}
			}

			if(!afflictionWoundHpToStack.empty())
				return makeActionForUnitSide(
					BattleAction::makeHeal(stack, afflictionWoundHpToStack.rbegin()->second));
			if(!controllerWoundHpToStack.empty())
				return makeActionForUnitSide(
					BattleAction::makeHeal(stack, controllerWoundHpToStack.rbegin()->second));
			return defend();
		}

		if(woundHpToStack.empty())
			return defend();
		return makeActionForUnitSide(
			BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second));
	}

	const auto healingOutput = battle->battleGetFirstAidHealingOutput(stack);
	if(healingOutput <= 0
		|| (currentControllerSide != BattleSide::ATTACKER && currentControllerSide != BattleSide::DEFENDER))
		return defend();

	using HealingRank = std::pair<int64_t, int64_t>;
	std::optional<std::pair<HealingRank, const CStack *>> bestControllerTarget;
	std::optional<std::pair<HealingRank, const CStack *>> bestAfflictionTarget;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		const auto * target = dynamic_cast<const CStack *>(unit);
		if(!target || !battle->battleCanHealWithFirstAidTent(stack, target))
			continue;

		const auto preview = battle->battleGetFirstAidHealingPreview(stack, target);
		if(preview.totalHealedHP() <= 0)
			continue;

		const auto missingHealth = std::max<int64_t>(0,
			target->getMaxHealth() - target->getFirstHPleft());
		const HealingRank rank{preview.totalHealedHP(), missingHealth};
		const auto candidate = std::pair{rank, target};
		if(!bestControllerTarget || candidate.first > bestControllerTarget->first)
			bestControllerTarget = candidate;
		if(surgeon && !battle->battleCanRepairWarMachine(stack, target)
			&& preview.survivorHealedHP > 0 && physicalAfflictions::first(*target)
			&& (!bestAfflictionTarget || candidate.first > bestAfflictionTarget->first))
			bestAfflictionTarget = candidate;
	}

	const auto selected = bestAfflictionTarget ? bestAfflictionTarget : bestControllerTarget;
	FirstAidStructureRepairPreview structure;
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
	{
		const auto candidate = battle->battleGetFirstAidStructureRepairPreview(stack, static_cast<EWallPart>(i));
		if(candidate.repairedHP() > structure.repairedHP())
			structure = candidate;
	}
	if(structure.repairedHP() > 0 && (!selected || structure.repairedHP() > selected->first.first))
	{
		BattleAction repair;
		repair.actionType = EActionType::STACK_HEAL;
		repair.side = currentControllerSide;
		repair.stackNumber = stack->unitId();
		repair.aimToHex(battle->wallPartToBattleHex(structure.part));
		return repair;
	}
	if(!selected)
		return defend();
	return makeActionForUnitSide(BattleAction::makeHeal(stack, selected->second));
}

void CBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	tacticsHandler->onTacticsStarted();
}

void CBattleAI::actionFinished(const BattleID & battleID, const BattleAction & action)
{
	tacticsHandler->onActionFinished(action);
}

static float getStrengthRatio(std::shared_ptr<CBattleInfoCallback> cb, BattleSide side)
{
	auto stacks = cb->battleGetAllStacks();
	auto our = 0;
	auto enemy = 0;

	for(auto stack : stacks)
	{
		auto creature = stack->creatureId().toCreature();

		if(!creature)
			continue;

		if(stack->unitSide() == side)
			our += stack->getCount() * creature->getAIValue();
		else
			enemy += stack->getCount() * creature->getAIValue();
	}

	return enemy == 0 ? 1.0f : static_cast<float>(our) / enemy;
}

int getSimulationTurnsCount(const StartInfo * startInfo)
{
	return startInfo->difficulty < 4 ? 2 : 10;
}

std::optional<BattleAction> chooseDemonicGate(const std::shared_ptr<CBattleInfoCallback> & battle,
	const CStack * source, BattleSide side)
{
	if(!battle || !source || source->unitSide() != side
		|| source->creatureId().toCreature()->getFactionID() != FactionID::INFERNO)
		return std::nullopt;
	const auto * hero = battle->battleGetFightingHero(side);
	if(!hero)
		return std::nullopt;
	const int rank = hero->getPerkSkillRank("new-horizons:demonicGating");
	if(rank <= 0)
		return std::nullopt;

	CreatureID chosen;
	uint64_t chosenValue = 0;
	for(const auto & [creature, count] : battle->getBattle()->getDemonicReserve(side))
	{
		const auto category = battle->battleGetCreatureCategory(creature);
		if(count <= 0 || !category || static_cast<int>(category->category) >= rank)
			continue;
		const uint64_t value = static_cast<uint64_t>(count) * creature.toCreature()->getAIValue();
		if(!chosen.hasValue() || value > chosenValue)
		{
			chosen = creature;
			chosenValue = value;
		}
	}
	if(!chosen.hasValue())
		return std::nullopt;

	const int placementRange = hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
	const bool mobileGate = hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate");
	const auto accessibility = battle->getAccessibility();
	std::vector<BattleHex> movementCandidates{source->getPosition()};
	if(mobileGate)
	{
		const auto movementAccessibility = battle->getAccessibility(source);
		const int movementLimit = static_cast<int>(source->getMovementRange(0) / 2);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			if(!candidate.isAvailable() || candidate == source->getPosition()
				|| !movementAccessibility.accessible(candidate, source))
				continue;
			const auto [path, distance] = battle->getPath(source->getPosition(), candidate, source);
			if(!path.empty() && distance >= 0 && distance <= movementLimit)
				movementCandidates.push_back(candidate);
		}
	}
	BattleHex best;
	BattleHex bestMovement = source->getPosition();
	int bestEnemyDistance = std::numeric_limits<int>::max();
	for(const auto movement : movementCandidates)
	{
		const BattleHex occupiedTail = source->doubleWide()
			? source->occupiedHex(movement) : BattleHex::INVALID;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			const BattleHex gatedTail = battle::Unit::occupiedHex(
				candidate, chosen.toCreature()->isDoubleWide(), side);
			if(!candidate.isAvailable() || candidate == movement || candidate == occupiedTail
				|| (gatedTail.isValid() && (gatedTail == movement || gatedTail == occupiedTail))
				|| BattleHex::getDistance(movement, candidate) > placementRange
				|| battle->battleGetUnitByPos(candidate, true)
				|| !battle->battleGetAllObstaclesOnPos(candidate, false).empty()
				|| !accessibility.accessible(candidate, chosen.toCreature()->isDoubleWide(), side))
				continue;
			int nearestEnemy = std::numeric_limits<int>::max();
			for(const auto * enemy : battle->battleAliveUnits(CBattleInfoEssentials::otherSide(side)))
				nearestEnemy = std::min(nearestEnemy, static_cast<int>(BattleHex::getDistance(candidate, enemy->getPosition())));
			if(nearestEnemy < bestEnemyDistance)
			{
				best = candidate;
				bestMovement = movement;
				bestEnemyDistance = nearestEnemy;
			}
		}
	}
	if(!best.isAvailable())
		return std::nullopt;

	BattleAction result;
	result.actionType = EActionType::DEMONIC_GATING;
	result.side = source->unitSide();
	result.stackNumber = source->unitId();
	result.gatingCreature = chosen;
	if(bestMovement != source->getPosition())
		result.aimToHex(bestMovement);
	result.aimToHex(best);
	return result;
}

BattleAction CBattleAI::choosePursuitMovement(const std::shared_ptr<CBattleInfoCallback> & battle,
	const CStack * source)
{
	const auto reachable = battle->battleGetAvailableHexes(source, false);
	const auto enemies = battle->battleAliveUnits(CBattleInfoEssentials::otherSide(source->unitSide()));
	BattleHex best = BattleHex::INVALID;
	int bestEnemyDistance = std::numeric_limits<int>::max();
	int bestTravelDistance = -1;
	const auto distances = battle->battleGetDistances(source, source->getPosition());
	for(const auto candidate : reachable)
	{
		if(candidate == source->getPosition())
			continue;
		int enemyDistance = std::numeric_limits<int>::max();
		for(const auto * enemy : enemies)
			for(const auto occupied : enemy->getHexes())
				enemyDistance = std::min(enemyDistance,
					static_cast<int>(BattleHex::getDistance(candidate, occupied)));
		const int travelDistance = distances[candidate.toInt()];
		if(enemyDistance < bestEnemyDistance
			|| (enemyDistance == bestEnemyDistance && travelDistance > bestTravelDistance))
		{
			best = candidate;
			bestEnemyDistance = enemyDistance;
			bestTravelDistance = travelDistance;
		}
	}
	return best.isAvailable() ? BattleAction::makeMove(source, best) : BattleAction::makeDefend(source);
}

void CBattleAI::activeStack(const BattleID & battleID, const CStack * stack )
{
	TimingScope timing(timingState.load(), battleID);
	LOG_TRACE_PARAMS(logAi, "stack: %s", stack->nodeName());
	const auto battleCallback = cb->getBattle(battleID);
	const auto hasMandatoryOrder = [&]()
	{
		return battleCallback->getBattle()->getCrisisCommandState().choice()
			|| battleCallback->battleHasPendingDoubleCommand(side)
			|| battleCallback->battleHasPendingPreCombatOrder(side);
	};
	if(hasMandatoryOrder())
	{
		// Mandatory opening/continuation Orders precede every creature action,
		// including the special siege, gating, and healing-tent paths below. The
		// evaluator keeps legal choices even when their ordinary heuristic is nonpositive.
		auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
		{
			return BattleEvaluator(env, cb, stack, playerID, battleID, side,
				getStrengthRatio(battleCallback, side),
				getSimulationTurnsCount(env->game()->getStartInfo()));
		});
		if(timing.measure(&TimingSummary::heroAction, [&]
		{
			return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
		}))
			return;
		if(hasMandatoryOrder())
		{
			if(battleCallback->getBattle()->getCrisisCommandState().choice())
			{
				auto decline = BattleAction::makeNoAction(stack);
				decline.side = side;
				timing.submitUnitAction(cb, battleID, decline);
				return;
			}
			// Never substitute a creature action while the authority still has an
			// unresolved mandatory Order window.
			logAi->error("BattleAI could not resolve a pending mandatory Hero Order");
			return;
		}
	}

	auto timeElapsed = [](std::chrono::time_point<std::chrono::high_resolution_clock> start) -> uint64_t
	{
		auto end = std::chrono::high_resolution_clock::now();

		return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	};

	BattleAction result = BattleAction::makeDefend(stack);

	auto start = std::chrono::high_resolution_clock::now();

	// Time Stop deliberately keeps a physical stack in the queue while making
	// every creature action illegal.  An AI still receives the same
	// non-automatic activation packet as a human, so it must use that window for
	// a legal Hero Action when one is available, or submit the authoritative
	// no-op pass.  Falling through to the ordinary stack evaluator would submit
	// DEFEND/WAIT and leave the battle waiting forever on a stopped stack.
	if(stack->isTimeStopped())
	{
		const auto battleCallback = cb->getBattle(battleID);
		if(battleCallback->battleGetMyHero()
			&& (autobattlePreferences.enableSpellsUsage || battleCallback->battleUsesHeroCommands()))
		{
			auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
			{
				return BattleEvaluator(env, cb, stack, playerID, battleID, side,
					getStrengthRatio(battleCallback, side),
					getSimulationTurnsCount(env->game()->getStartInfo()));
			});
			if(evaluator.canCastSpell() && timing.measure(&TimingSummary::heroAction, [&]
			{
				return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
			}))
				return;
		}

		BattleAction pass = BattleAction::makeNoAction(stack);
		// The AI callback is dispatched for the action controller, while every
		// creature-action packet continues to identify the stack's physical side.
		pass.side = stack->unitSide();
		timing.submitUnitAction(cb, battleID, pass);
		return;
	}

	if(stack->pursuitMovementRemaining > 0)
	{
		const auto battleCallback = cb->getBattle(battleID);
		if(battleCallback->battleGetMyHero()
			&& (autobattlePreferences.enableSpellsUsage || battleCallback->battleUsesHeroCommands()))
		{
			auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
			{
				return BattleEvaluator(env, cb, stack, playerID, battleID, side,
					getStrengthRatio(battleCallback, side),
					getSimulationTurnsCount(env->game()->getStartInfo()));
			});
			if(evaluator.canCastSpell()
				&& timing.measure(&TimingSummary::heroAction, [&]
				{
					return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
				}))
				return;
		}
		timing.submitUnitAction(cb, battleID, choosePursuitMovement(battleCallback, stack));
		return;
	}

	if(stack->isCatapult())
	{
		timing.submitUnitAction(cb, battleID, useCatapult(battleID, stack));
		return;
	}
	if(auto gating = chooseDemonicGate(cb->getBattle(battleID), stack, side))
	{
		timing.submitUnitAction(cb, battleID, *gating);
		return;
	}
	if(stack->hasBonusOfType(BonusType::SIEGE_WEAPON) && stack->hasBonusOfType(BonusType::HEALER))
	{
		timing.submitUnitAction(cb, battleID, useHealingTent(battleID, stack));
		return;
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Build evaluator and targets");
#endif

	auto evaluator = timing.measure(&TimingSummary::evaluatorConstruction, [&]
	{
		return BattleEvaluator(env, cb, stack, playerID, battleID, side,
			getStrengthRatio(cb->getBattle(battleID), side),
			getSimulationTurnsCount(env->game()->getStartInfo()));
	});

	result = timing.measure(&TimingSummary::stackActionSelection, [&]
	{
		return evaluator.selectStackAction(stack);
	});

	if((autobattlePreferences.enableSpellsUsage || cb->getBattle(battleID)->battleUsesHeroCommands()) && evaluator.canCastSpell())
	{
		auto spelCasted = timing.measure(&TimingSummary::heroAction, [&]
		{
			return evaluator.attemptCastingSpell(stack, autobattlePreferences.enableSpellsUsage);
		});

		if(spelCasted)
			return;
	}

	// Master Gunner's saved allowance is a same-activation continuation, not a
	// new creature turn. Hero Actions above may still be used first; after that,
	// the only creature action is a fresh, independently selected legal shot.
	// If the raw allowance is no longer usable (for example, every enemy became
	// untargetable after a Hero Action), pass it instead of falling through to a
	// move/wait/defend that the authority must reject.
	if(const auto unitState = stack->acquireState(); unitState && unitState->rangedFollowUpDamagePercent > 0)
	{
		const auto battleCallback = cb->getBattle(battleID);
		const battle::Unit * bestTarget = nullptr;
		int64_t bestExpectedDamage = std::numeric_limits<int64_t>::min();
		if(battleCallback->battleCanTakeRangedFollowUp(stack))
		{
			for(const auto * target : battleCallback->battleAliveUnits())
			{
				if(!target || !target->alive() || target->isGhost()
					|| battleCallback->battleMatchActionController(stack, target, true))
					continue;

				const bool hasLegalHex = std::ranges::any_of(target->getHexes(), [&](const BattleHex & hex)
				{
					return hex.isValid() && battleCallback->battleCanShootAction(stack, hex);
				});
				if(!hasLegalHex)
					continue;

				const auto expectedDamage = battleCallback->battleExpectedLuckDamage(
					BattleAttackInfo(stack, target, 0, true));
				if(!bestTarget || expectedDamage > bestExpectedDamage)
				{
					bestTarget = target;
					bestExpectedDamage = expectedDamage;
				}
			}
		}

		if(bestTarget)
		{
			BattleAction followUp = BattleAction::makeShotAttack(stack, bestTarget);
			timing.submitUnitAction(cb, battleID, followUp);
			return;
		}

		BattleAction pass = BattleAction::makeNoAction(stack);
		timing.submitUnitAction(cb, battleID, pass);
		return;
	}

	logAi->trace("Spellcast attempt completed in %lld", timeElapsed(start));

	if(auto action = considerFleeingOrSurrendering(battleID))
	{
		timing.submitUnitAction(cb, battleID, *action);
		return;
	}

	// Creature Catapult abilities are legal stack actions, distinct from the
	// Catapult war-machine branch above. Consider one only after ordinary Hero
	// Actions and typed continuations have had their opportunity to resolve.
	const auto currentBattle = cb->getBattle(battleID);
	if(currentBattle && shouldUseCreatureCatapult(*currentBattle, stack, result, playerID))
		result = useCatapult(battleID, stack);

	if(result.actionType == EActionType::DEFEND)
	{
		movesSkippedByDefense++;
	}
	else if(result.actionType != EActionType::WAIT)
	{
		movesSkippedByDefense = 0;
	}

	logAi->trace("BattleAI decision made in %lld", timeElapsed(start));

	timing.submitUnitAction(cb, battleID, result);
}

bool CBattleAI::shouldUseCreatureCatapult(CBattleInfoCallback & battle,
	const CStack * stack, const BattleAction & ordinaryAction, PlayerColor actionController)
{
	if(!stack || !stack->alive() || stack->isGhost() || stack->isTurret()
		|| stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| !stack->hasBonusOfType(BonusType::CATAPULT)
		|| battle.battleGetActionController(stack) != actionController)
		return false;
	if(ordinaryAction.actionType == EActionType::WAIT)
		return false;

	const auto * battleState = battle.getBattle();
	if(!battleState || battleState->getActiveStackID() < 0
		|| stack->unitId() != static_cast<uint32_t>(battleState->getActiveStackID()))
		return false;

	const BattleClientInterfaceData clientData{{}, 0};
	const auto legalActions = battle.getClientActionsForStack(stack, clientData);
	const PossiblePlayerBattleAction catapultAction{PossiblePlayerBattleAction::CATAPULT};
	if(std::ranges::find(legalActions, catapultAction) == legalActions.end())
		return false;

	const auto * defendedTown = battle.battleGetDefendedTown();
	if(!defendedTown || defendedTown->fortificationsLevel().wallsHealth <= 0)
		return false;

	const BattleSide actionSide = battle.playerToSide(actionController);
	const BattleSide townSide = battle.playerToSide(defendedTown->tempOwner);
	if((actionSide != BattleSide::ATTACKER && actionSide != BattleSide::DEFENDER)
		|| townSide != CBattleInfoEssentials::otherSide(actionSide))
		return false;

	if(battle.battleGetCatapultStructuralDamage(stack, 1) <= 0)
		return false;

	static constexpr std::array attackableWallParts{
		EWallPart::KEEP,
		EWallPart::BOTTOM_TOWER,
		EWallPart::UPPER_TOWER,
		EWallPart::BELOW_GATE,
		EWallPart::OVER_GATE,
		EWallPart::BOTTOM_WALL,
		EWallPart::UPPER_WALL,
		EWallPart::GATE
	};
	const bool hasUsefulStructure = std::ranges::any_of(attackableWallParts, [&](EWallPart part)
	{
		return battle.isWallPartAttackable(part) && battle.getWallStructuralHP(part) > 0;
	});
	if(!hasUsefulStructure)
		return false;

	const bool gateNeedsBreaching = battle.battleGetGateState() == EGateState::CLOSED
		&& battle.isWallPartAttackable(EWallPart::GATE)
		&& battle.getWallStructuralHP(EWallPart::GATE) > 0;
	if(gateNeedsBreaching)
	{
		bool alliedGroundForceOutside = false;
		bool hostileStackInside = false;
		for(const auto * candidate : battle.battleGetAllStacks())
		{
			if(!candidate || !candidate->alive() || candidate->isGhost() || candidate->isTurret()
				|| candidate->unitId() == stack->unitId())
				continue;

			const auto candidateOwnerSide = battle.playerToSide(battle.battleGetOwner(candidate));
			const auto candidateControllerSide = battle.playerToSide(battle.battleGetActionController(candidate));
			const bool hasOutsideFootprint = std::ranges::any_of(candidate->getHexes(), [&](const BattleHex & hex)
			{
				return hex.isValid() && !battle.battleIsInsideWalls(hex);
			});
			const bool hasInsideFootprint = std::ranges::any_of(candidate->getHexes(), [&](const BattleHex & hex)
			{
				return hex.isValid() && battle.battleIsInsideWalls(hex);
			});

			// Count only a controlled allied ground melee force outside the walls;
			// neither the acting Cyclops itself, shooters, siege engines, nor a
			// Puppet-controlled unit on the opposing side creates a breach need.
			if(candidateOwnerSide == actionSide && candidateControllerSide == actionSide
				&& !candidate->hasBonusOfType(BonusType::FLYING) && !candidate->isShooter()
				&& !candidate->hasBonusOfType(BonusType::SIEGE_WEAPON) && candidate->isMeleeAttacker()
				&& hasOutsideFootprint)
				alliedGroundForceOutside = true;

			const auto hostileSide = CBattleInfoEssentials::otherSide(actionSide);
			if(candidateOwnerSide == hostileSide && candidateControllerSide == hostileSide
				&& hasInsideFootprint)
				hostileStackInside = true;
		}

		if(alliedGroundForceOutside && hostileStackInside)
			return true;
	}

	// Otherwise preserve ordinary valuable attacks and use an available wall
	// shot only when the evaluator found no attack, or selected movement/defense.
	return ordinaryAction.actionType == EActionType::DEFEND
		|| ordinaryAction.actionType == EActionType::WALK;
}

BattleAction CBattleAI::useCatapult(const BattleID & battleID, const CStack * stack)
{
	BattleAction attack;
	BattleHex targetHex = BattleHex::INVALID;
	const auto battle = cb->getBattle(battleID);
	const auto currentControllerSide = battle->playerToSide(battle->battleGetActionController(stack));

	if(battle->battleGetGateState() == EGateState::CLOSED)
	{
		const auto gateHex = battle->wallPartToBattleHex(EWallPart::GATE);
		if(gateHex.isValid() && battle->isWallPartAttackable(EWallPart::GATE))
			targetHex = gateHex;
	}
	if(!targetHex.isValid())
	{
		std::array wallParts {
			EWallPart::KEEP,
			EWallPart::BOTTOM_TOWER,
			EWallPart::UPPER_TOWER,
			EWallPart::BELOW_GATE,
			EWallPart::OVER_GATE,
			EWallPart::BOTTOM_WALL,
			EWallPart::UPPER_WALL
		};

		const auto structuralOutput = std::max<int32_t>(0, battle->battleGetCatapultStructuralDamage(stack, 1));
		int32_t bestDamage = -1;
		for(auto wallPart : wallParts)
		{
			if(!battle->isWallPartAttackable(wallPart))
				continue;

			const auto candidateHex = battle->wallPartToBattleHex(wallPart);
			if(!candidateHex.isValid())
				continue;

			const auto wallHp = std::max<int32_t>(0, battle->getWallStructuralHP(wallPart));
			auto predictedDamage = wallHp > 0 && structuralOutput > 0
				? std::min(wallHp, structuralOutput) : 0;
			const auto overflow = battle->battleGetBreachmakerPreview(stack, wallPart, structuralOutput);
			if(overflow.damage > 0)
				predictedDamage += std::min(overflow.damage, battle->getWallStructuralHP(overflow.part));
			if(!targetHex.isValid() || predictedDamage > bestDamage)
			{
				targetHex = candidateHex;
				bestDamage = predictedDamage;
			}
		}
	}

	if(!targetHex.isValid())
	{
		auto defend = BattleAction::makeDefend(stack);
		defend.side = stack->unitSide();
		return defend;
	}

	attack.aimToHex(targetHex);
	attack.actionType = EActionType::CATAPULT;
	attack.side = stack->unitSide();
	attack.stackNumber = stack->unitId();

	movesSkippedByDefense = 0;

	return attack;
}

void CBattleAI::battleStart(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide Side, bool replayAllowed)
{
	timingState.store(std::make_shared<TimingState>(battleID, playerID));
	LOG_TRACE(logAi);
	side = Side;
	auto tacticsSettings = TacticsHandler::Settings{.enabled = autobattlePreferences.enableTacticsUsage};
	tacticsHandler = std::make_unique<TacticsHandler>(cb, battleID, tacticsSettings);
}

void CBattleAI::battleEnd(const BattleID & battleID, const BattleResult * result, QueryID queryID)
{
	const auto current = timingState.load();
	if(!current || current->battleID != battleID)
		return;
	std::optional<TimingSummary> report;
	{
		std::lock_guard lock(current->mutex);
		if(current->summary.finished)
			return;
		current->ended = TimingState::Clock::now();
		current->summary.finished = true;
		report = current->reportLocked();
	}
	current->log(report);
}

void CBattleAI::battleNewRound(const BattleID & battleID)
{
	const auto current = timingState.load();
	const auto battle = cb->getBattle(battleID);
	if(!current || current->battleID != battleID || !battle)
		return;
	const auto round = battle->battleGetRound();
	std::lock_guard lock(current->mutex);
	if(!current->summary.finished && round >= 0 && round != current->lastObservedRound)
	{
		current->lastObservedRound = round;
		++current->summary.roundsObserved;
	}
}

void CBattleAI::print(const std::string &text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}

std::optional<BattleAction> CBattleAI::considerFleeingOrSurrendering(const BattleID & battleID)
{
	BattleStateInfoForRetreat bs;

	bs.canFlee = cb->getBattle(battleID)->battleCanFlee();
	bs.canSurrender = cb->getBattle(battleID)->battleCanSurrender(playerID);
	bs.ourSide = cb->getBattle(battleID)->battleGetMySide();
	bs.ourHero = cb->getBattle(battleID)->battleGetMyHero();
	bs.enemyHero = nullptr;

	for(auto stack : cb->getBattle(battleID)->battleGetAllStacks(false))
	{
		if(stack->alive())
		{
			if(stack->unitSide() == bs.ourSide)
				bs.ourStacks.push_back(stack);
			else
			{
				bs.enemyStacks.push_back(stack);
				bs.enemyHero = cb->getBattle(battleID)->battleGetOwnerHero(stack);
			}
		}
	}

	bs.turnsSkippedByDefense = movesSkippedByDefense / bs.ourStacks.size();

	if(!bs.canFlee && !bs.canSurrender)
	{
		return std::nullopt;
	}

	auto result = cb->makeSurrenderRetreatDecision(battleID, bs);

	if(!result && bs.canFlee && bs.turnsSkippedByDefense > 30)
	{
		return BattleAction::makeRetreat(bs.ourSide);
	}

	return result;
}
