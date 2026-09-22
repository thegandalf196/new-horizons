/*
 * BattleFlowProcessor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleFlowProcessor.h"

#include "BattleProcessor.h"

#include "../CGameHandler.h"
#include "../TurnTimerHandler.h"

#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/BonusCaster.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/CObstacleInstance.h"

#include <vstd/RNG.h>

namespace
{
	std::optional<bool> canonicalWarMachineControl(const CGHeroInstance * hero, CreatureID machine)
	{
		if(!hero || !newHorizonsHeroes::usesRules(hero->getCapabilityRules())
			|| hero->getCapabilityRules()["rulesetVersion"].Integer() < 3)
			return std::nullopt;
		const auto siege = hero->getSiegeCapabilities();
		if(!siege)
			return std::nullopt;
		if(machine == CreatureID::BALLISTA)
			return siege->ballistaControlChance >= 100;
		if(machine == CreatureID::CATAPULT)
			return siege->catapultControlChance >= 100;
		if(machine == CreatureID::FIRST_AID_TENT)
			return siege->firstAidControlChance >= 100;
		return std::nullopt;
	}

	bool allSurvivingStacksTimeStopped(const CBattleInfoCallback & battle)
	{
		bool foundAlive = false;
		for(const auto * stack : battle.battleGetAllStacks(true))
		{
			if(!stack || !stack->alive())
				continue;
			foundAlive = true;
			if(!stack->isTimeStopped())
				return false;
		}
		return foundAlive;
	}

	std::optional<BattleSide> timeStopMarkerSide(const battle::Unit & unit)
	{
		for(const auto & marker : *unit.getBonuses(Selector::type()(BonusType::TIME_STOP)))
		{
			if(!marker || !marker->parameters)
				continue;
			try
			{
				const auto side = static_cast<BattleSide>(marker->parameters->toNumber());
				if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
					return side;
			}
			catch(const std::exception &)
			{
				// A malformed/legacy marker has no reliable caster side. The
				// caller will fall back to the ordinary queue order.
			}
		}
		return std::nullopt;
	}

	const CStack * stoppedHeroActionAnchor(const CBattleInfoCallback & battle, BattleSide requiredSide = BattleSide::NONE)
	{
		const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle());
		const auto stacks = battle.battleGetAllStacks(true);
		const auto ownerSide = [&battle](const CStack * stack)
		{
			return stack ? battle.playerToSide(battle.battleGetOwner(stack)) : BattleSide::NONE;
		};
		const auto findControllable = [&stacks, &ownerSide, &battle](BattleSide side, bool requireHero)
			-> const CStack *
		{
			if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
				return nullptr;
			if(requireHero && !battle.battleGetFightingHero(side))
				return nullptr;
			for(const auto * stack : stacks)
				if(stack && stack->alive() && stack->isTimeStopped() && ownerSide(stack) == side)
					return stack;
			return nullptr;
		};
		if(requiredSide == BattleSide::ATTACKER || requiredSide == BattleSide::DEFENDER)
		{
			if(const auto * candidate = findControllable(requiredSide, true))
				return candidate;
			return findControllable(requiredSide, false);
		}

		// Prefer a stopped stack owned by the side whose pending origin is due,
		// and prefer a side with a fighting hero so the resulting TURN_QUEUE
		// activation can submit a legal Hero Action.  The owner check deliberately
		// uses battleGetOwner(), which accounts for hypnotized stacks.
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			if(state && state->hasPendingTimeStopHeroAction(side))
				if(const auto * candidate = findControllable(side, true))
					return candidate;
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			if(state && state->hasPendingTimeStopHeroAction(side))
				if(const auto * candidate = findControllable(side, false))
					return candidate;
		}

		for(const auto * stack : stacks)
		{
			if(!stack || !stack->alive() || !stack->isTimeStopped())
				continue;
			const auto markerSide = timeStopMarkerSide(*stack);
			if(!markerSide)
				continue;
			for(const auto * candidate : stacks)
				if(candidate && candidate->alive() && candidate->isTimeStopped() && ownerSide(candidate) == *markerSide)
					return candidate;
		}

		for(const auto * stack : stacks)
			if(stack && stack->alive() && stack->isTimeStopped())
				return stack;
		return nullptr;
	}

	std::optional<BattleSide> pendingStoppedSideAtRoundBoundary(const CBattleInfoCallback & battle)
	{
		const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle());
		if(!state)
			return std::nullopt;

		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			if(!state->hasPendingTimeStopHeroAction(side))
				continue;

			bool foundControlledStack = false;
			bool everyControlledStackStopped = true;
			for(const auto * stack : battle.battleGetAllStacks(true))
			{
				if(!stack || !stack->alive() || battle.playerToSide(battle.battleGetOwner(stack)) != side)
					continue;
				foundControlledStack = true;
				everyControlledStackStopped = everyControlledStackStopped && stack->isTimeStopped();
			}

			if(foundControlledStack && everyControlledStackStopped && stoppedHeroActionAnchor(battle, side))
				return side;
		}
		return std::nullopt;
	}

	// Legacy obstacle callbacks are movement-driven.  Canonical New Horizons
	// Fire Wall additionally triggers at activation start, but dispatching the
	// generic callback for every activation would alter legacy obstacle timing.
	// Keep this check narrow so only a unit currently standing on a canonical
	// Fire Wall receives the extra activation callback.
	bool canonicalFireWallCoversUnit(const CBattleInfoCallback & battle, const battle::Unit & unit)
	{
		if(!newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules()))
			return false;

		for(const auto & hex : unit.getHexes())
		{
			for(const auto & obstacle : battle.battleGetAllObstaclesOnPos(hex, false))
			{
				const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
				if(spellObstacle && newHorizonsMagic::isFireWall(SpellID(spellObstacle->ID)))
					return true;
			}
		}

		return false;
	}
}

BattleFlowProcessor::BattleFlowProcessor(BattleProcessor * owner, CGameHandler * newGameHandler)
	: owner(owner)
	, gameHandler(newGameHandler)
{
}

void BattleFlowProcessor::publishHeroOrderState(const CBattleInfoCallback & battle, BattleSide side) const
{
	if(!heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return;
	BattleHeroOrderStateChanged update;
	update.battleID = battle.getBattle()->getBattleID();
	update.side = side;
	update.state = battle.battleGetHeroOrderState(side);
	gameHandler->sendAndApply(update);
}

void BattleFlowProcessor::tryPlaceMoats(const CBattleInfoCallback & battle)
{
	const auto * town = battle.battleGetDefendedTown();

	if (!town)
		return;

	const auto & fortifications = town->fortificationsLevel();

	//Moat should be initialized here, because only here we can use spellcasting
	if (fortifications.hasMoat)
	{
		const auto * h = battle.battleGetFightingHero(BattleSide::DEFENDER);
		const auto * actualCaster = h ? static_cast<const spells::Caster*>(h) : nullptr;
		auto moatCaster = spells::SilentCaster(battle.sideToPlayer(BattleSide::DEFENDER), actualCaster);
		auto cast = spells::BattleCast(&battle, &moatCaster, spells::Mode::PASSIVE, fortifications.moatSpell.toSpell());
		auto target = spells::Target();
		cast.cast(gameHandler->spellcastEnvironment(), target);
	}
}

void BattleFlowProcessor::onBattleStarted(const CBattleInfoCallback & battle)
{
	// before anything acts and before tactics, so that a unit whose stats depend on the battle it
	// finds itself in - an arrow tower reading its town - is right from the first frame the player sees
	for(const CStack * stack : battle.battleGetAllStacks(true))
		owner->processBattleEventTriggers(battle, CombatEventType::BATTLE_SETUP, stack, nullptr);

	tryPlaceMoats(battle);

	gameHandler->turnTimerHandler->onBattleStart(battle.getBattle()->getBattleID());

	if (battle.battleGetTacticDist() == 0)
		onTacticsEnded(battle);
}

void BattleFlowProcessor::castOpeningSpells(const CBattleInfoCallback & battle)
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * h = battle.battleGetFightingHero(i);
		if (!h)
			continue;

		TConstBonusListPtr bl = h->getBonusesOfType(BonusType::OPENING_BATTLE_SPELL);

		for (const auto & b : *bl)
		{
			spells::BonusCaster caster(h, b);

			SpellID spellID = b->subtype.as<SpellID>();
			if (!spellID.hasValue())
			{
				logGlobal->error("unable to cast spell - OPENING_BATTLE_SPELL has invalid spell set!");
				continue;
			}
			const CSpell * spell = spellID.toSpell();

			spells::BattleCast parameters(&battle, &caster, spells::Mode::PASSIVE, spell);
			int32_t spellLevel = b->parameters ? b->parameters->toNumber() : 3;
			parameters.setSpellLevel(spellLevel);
			parameters.setEffectDuration(b->val);
			parameters.forceMassive = true;
			parameters.castIfPossible(gameHandler->spellcastEnvironment(), spells::Target());
		}
	}
}

void BattleFlowProcessor::onTacticsEnded(const CBattleInfoCallback & battle)
{
	//initial stacks appearance triggers, e.g. built-in bonus spells
	auto initialStacks = battle.battleGetAllStacks(true);

	for (const CStack * stack : initialStacks)
	{
		owner->processBattleEventTriggers(battle, CombatEventType::BATTLE_START, stack, nullptr);
	}

	castOpeningSpells(battle);

	// it is possible that due to opening spells one side was eliminated -> check for end of battle
	if (owner->checkBattleStateChanges(battle))
		return;

	startNextRound(battle, true);
	activateNextStack(battle);
}

void BattleFlowProcessor::startNextRound(const CBattleInfoCallback & battle, bool isFirstRound)
{
	// Swift Gate resolves while the current round still exists. Ordinary Gates
	// resolve only after BattleNextRound advances the authoritative round.
	resolveDemonicGates(battle, true);
	BattleNextRound bnr;
	bnr.battleID = battle.getBattle()->getBattleID();
	logGlobal->debug("Next round starts");
	gameHandler->sendAndApply(bnr);
	resolveDemonicGates(battle, false);

	// operate on copy - removing obstacles will invalidate iterator on 'battle' container
	auto obstacles = battle.battleGetAllObstacles();
	for (const auto & obstPtr : obstacles)
	{
		const auto * sco = dynamic_cast<const SpellCreatedObstacle *>(obstPtr.get());
		if (sco && sco->turnsRemaining == 0)
			removeObstacle(battle, *obstPtr);
	}

	// first round is covered by the battle start triggers, which ran just before this
	if(!isFirstRound)
	{
		for(const auto * stack : battle.battleGetAllStacks(true))
			if(stack->alive() && !stack->isTimeStopped())
				owner->processBattleEventTriggers(battle, CombatEventType::ROUND_START, stack, nullptr);
	}
}

void BattleFlowProcessor::resolveDemonicGates(const CBattleInfoCallback & battle, bool endOfRoundPhase)
{
	for(const auto sideId : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
		if(!concrete)
			continue;
		const auto snapshot = concrete->getSide(sideId);
		if(snapshot.pendingDemonicGates.empty())
			continue;
		const auto * hero = battle.battleGetFightingHero(sideId);
		const bool swiftGate = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.swiftGate");
		const bool hellfireArrival = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.hellfireArrival");

		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = sideId;
		update.reserve = snapshot.demonicReserve;
		update.gated = snapshot.gatedDemonicStacks;

		for(const auto & gate : snapshot.pendingDemonicGates)
		{
			const bool due = endOfRoundPhase
				? swiftGate && gate.arrivalRound <= concrete->getRound() + 1
				: gate.arrivalRound <= concrete->getRound();
			if(!due)
			{
				update.pending.push_back(gate);
				continue;
			}

			const auto * creature = gate.creature.toCreature();
			if(!creature || gate.count <= 0)
				continue;
			auto accessibility = battle.getAccessibility();
			BattleHex arrival = gate.position;
			if(!accessibility.accessible(arrival, creature->isDoubleWide(), sideId))
			{
				BattleHex best;
				uint8_t bestDistance = std::numeric_limits<uint8_t>::max();
				for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
				{
					BattleHex candidate(index);
					if(!candidate.isAvailable() || !accessibility.accessible(candidate, creature->isDoubleWide(), sideId))
						continue;
					const auto distance = BattleHex::getDistance(gate.position, candidate);
					if(distance < bestDistance)
					{
						best = candidate;
						bestDistance = distance;
					}
				}
				arrival = best;
			}
			if(!arrival.isAvailable())
			{
				update.pending.push_back(gate);
				continue;
			}

			battle::UnitInfo info;
			info.id = concrete->battleNextUnitId();
			info.count = gate.count;
			info.type = gate.creature;
			info.side = sideId;
			info.position = arrival;
			// These are real owned troops and must inherit ordinary hero bonuses.
			// Their unit IDs are tracked separately so post-battle reserve
			// reconciliation, rather than the summoned-creature path, owns them.
			info.summoned = false;
			BattleUnitsChanged add;
			add.battleID = concrete->getBattleID();
			add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
			info.save(add.changedStacks.back().data);
			gameHandler->sendAndApply(add);
			update.gated.push_back({info.id, gate.creature, gate.count});

			if(hellfireArrival)
			{
				const auto * gated = battle.battleGetStackByID(info.id, false);
				std::vector<const battle::Unit *> enemies;
				if(gated)
					for(const auto * adjacent : battle.battleAdjacentUnits(gated))
						if(adjacent && adjacent->alive() && adjacent->unitSide() != sideId)
							enemies.push_back(adjacent);
				const int64_t totalFireDamage = gated ? gated->getAvailableHealth() * 15 / 100 : 0;
				const int64_t damagePerEnemy = enemies.empty() ? 0 : totalFireDamage / enemies.size();
				if(damagePerEnemy > 0)
				{
					StacksInjured injury;
					injury.battleID = concrete->getBattleID();
					int64_t appliedDamage = 0;
					for(const auto * enemy : enemies)
					{
						BattleStackAttacked hit;
						hit.attackerID = info.id;
						hit.stackAttacked = enemy->unitId();
						hit.damageAmount = damagePerEnemy;
						hit.flags |= BattleStackAttacked::SPELL_EFFECT;
						CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), enemy->acquireState());
						appliedDamage += hit.damageAmount;
						injury.stacks.push_back(hit);
					}
					gameHandler->sendAndApply(injury);
					BattleLogMessage hellfireLog;
					hellfireLog.battleID = concrete->getBattleID();
					MetaString hellfireLine = MetaString::createFromRawString("Hellfire deals ");
					hellfireLine.appendNumber(appliedDamage);
					hellfireLine.appendRawString(" damage.");
					hellfireLog.lines.push_back(std::move(hellfireLine));
					gameHandler->sendAndApply(hellfireLog);
				}
			}
			BattleLogMessage message;
			message.battleID = concrete->getBattleID();
			MetaString line = MetaString::createFromRawString("The Gate brings forth ");
			line.appendNumber(gate.count);
			line.appendRawString(" ");
			line.appendName(gate.creature, gate.count);
			line.appendRawString(".");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		gameHandler->sendAndApply(update);
	}
}

const CStack * BattleFlowProcessor::getNextStack(const CBattleInfoCallback & battle)
{
	std::vector<battle::Units> q;
	battle.battleGetTurnOrder(q, 1, 0, -1); //todo: get rid of "turn -1"

	if(q.empty())
		return nullptr;

	if(q.front().empty())
		return nullptr;

	const auto * next = q.front().front();
	const auto * stack = dynamic_cast<const CStack *>(next);

	// regeneration takes place before everything else but only during first turn attempt in each round
	// also works under blind and similar effects
	if(stack && stack->alive() && !stack->waiting && !stack->isTimeStopped())
	{
		BattleTriggerEffect bte;
		bte.battleID = battle.getBattle()->getBattleID();
		bte.stackID = stack->unitId();
		bte.effect = BonusType::HP_REGENERATION;

		const int32_t lostHealth = stack->getMaxHealth() - stack->getFirstHPleft();
		if(stack->hasBonusOfType(BonusType::HP_REGENERATION))
			bte.val = std::min(lostHealth, stack->valOfBonuses(BonusType::HP_REGENERATION));

		if(bte.val) // anything to heal
			gameHandler->sendAndApply(bte);
	}

	if(!next || (!next->willMove() && !(next->isTimeStopped() && !next->timeStopTurnConsumed())))
		return nullptr;

	return stack;
}

void BattleFlowProcessor::activateNextStack(const CBattleInfoCallback & battle)
{
	// Find next stack that requires manual control
	for (;;)
	{
		// battle has ended
		if (owner->checkBattleStateChanges(battle))
			return;

		const CStack * next = getNextStack(battle);

		if (!next)
		{
			// No creature stacks remain in this round. A battle in which every
			// survivor is stopped must still cross the round boundary once, then
			// expose a normal (non-automatic) activation so the caster's player or
			// AI can submit a Hero Action. AUTOMATIC_ACTION is intentionally not
			// used here: clients ignore it as a control request.
			const bool allStopped = allSurvivingStacksTimeStopped(battle);
			startNextRound(battle, false);
			if(owner->checkBattleStateChanges(battle))
				return;
			// An origin can remain pending after another side's Time Stop has
			// expired. If every stack controlled by that origin is still stopped,
			// do not let ordinary opponent-side queue entries run indefinitely;
			// expose this side's Hero Action at the new round boundary first.
			if(const auto pendingSide = pendingStoppedSideAtRoundBoundary(battle))
			{
				const auto * anchor = stoppedHeroActionAnchor(battle, *pendingSide);
				if(!anchor)
					throw std::runtime_error("Failed to find a pending Time Stop origin anchor");
				gameHandler->turnTimerHandler->onBattleNextStack(battle.getBattle()->getBattleID(), *anchor);
				setActiveStack(battle, anchor, BattleUnitTurnReason::TURN_QUEUE);
				return;
			}
			if(allStopped)
			{
				const auto * anchor = stoppedHeroActionAnchor(battle);
				if(!anchor)
					throw std::runtime_error("Failed to find a stopped stack for the next Hero Action");
				gameHandler->turnTimerHandler->onBattleNextStack(battle.getBattle()->getBattleID(), *anchor);
				setActiveStack(battle, anchor, BattleUnitTurnReason::TURN_QUEUE);
				return;
			}
			next = getNextStack(battle);
			if (!next)
				throw std::runtime_error("Failed to find valid stack to act!");
		}

		BattleUnitsChanged removeGhosts;
		removeGhosts.battleID = battle.getBattle()->getBattleID();

		auto pendingGhosts = battle.battleGetStacksIf([](const CStack * stack){
			return stack->ghostPending;
		});

		for(const auto * stack : pendingGhosts)
			removeGhosts.changedStacks.emplace_back(stack->unitId(), UnitChanges::EOperation::REMOVE);

		if(!removeGhosts.changedStacks.empty())
			gameHandler->sendAndApply(removeGhosts);

		gameHandler->turnTimerHandler->onBattleNextStack(battle.getBattle()->getBattleID(), *next);

		if (!tryMakeAutomaticAction(battle, next))
		{
			if(next->alive()) {
				setActiveStack(battle, next, BattleUnitTurnReason::TURN_QUEUE);
				if(next->alive())
					break;
			}
		}
	}
}

bool BattleFlowProcessor::tryMakeAutomaticAction(const CBattleInfoCallback & battle, const CStack * next)
{
	// A stopped stack still needs an automatic no-op so the turn queue can
	// advance, but it must not run morale/berserk/poison/enchanter triggers or
	// any other activation-side effect while outside time.
	if(next->isTimeStopped())
	{
		return makeStackDoNothing(battle, next);
	}

	if(tryActivateMoralePenalty(battle, next))
		return true;

	if(tryActivateBerserkPenalty(battle, next))
		return true;

	if(handleForcedCpuControlledUnit(battle, next))
		return true;

	stackTurnTrigger(battle, next); //various effects

	if(next->fear)
	{
		makeStackDoNothing(battle, next); //end immediately if stack was affected by fear
		return true;
	}

	return false;
}

bool BattleFlowProcessor::tryActivateMoralePenalty(const CBattleInfoCallback & battle, const CStack * next)
{
	// check for bad morale => freeze
	int nextStackMorale = next->moraleVal();
	if(!next->hadMorale && !next->waited() && nextStackMorale < 0)
	{
		ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(next->unitSide())->id;
		if (gameHandler->randomizer->rollBadMorale(ownerArmy, -nextStackMorale))
		{
			//unit loses its turn - empty freeze action
			BattleAction ba;
			ba.actionType = EActionType::BAD_MORALE;
			ba.side = next->unitSide();
			ba.stackNumber = next->unitId();

			makeAutomaticAction(battle, next, ba);
			return true;
		}
	}
	return false;
}

bool BattleFlowProcessor::tryActivateBerserkPenalty(const CBattleInfoCallback & battle, const CStack * next)
{
	if (next->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)) //while in berserk
	{
		ForcedAction forcedAction = battle.getBerserkForcedAction(next);
		if (forcedAction.type == EActionType::SHOOT)
		{
			BattleAction rangeAttack;
			rangeAttack.actionType = EActionType::SHOOT;
			rangeAttack.side = next->unitSide();
			rangeAttack.stackNumber = next->unitId();
			rangeAttack.aimToUnit(forcedAction.target);
			makeAutomaticAction(battle, next, rangeAttack);
		}
		else if (forcedAction.type == EActionType::WALK_AND_ATTACK)
		{
			BattleAction meleeAttack;
			meleeAttack.actionType = EActionType::WALK_AND_ATTACK;
			meleeAttack.side = next->unitSide();
			meleeAttack.stackNumber = next->unitId();
			meleeAttack.aimToHex(forcedAction.position);
			meleeAttack.aimToUnit(forcedAction.target);
			makeAutomaticAction(battle, next, meleeAttack);
		} else if (forcedAction.type == EActionType::WALK)
		{
			BattleAction movement;
			movement.actionType = EActionType::WALK;
			movement.stackNumber = next->unitId();
			movement.aimToHex(forcedAction.position);
			makeAutomaticAction(battle, next, movement);
		}
		else
		{
			makeStackDoNothing(battle, next);
		}
		return true;
	}
	return false;
}

bool BattleFlowProcessor::handleForcedCpuControlledUnit(const CBattleInfoCallback & battle, const CStack * next)
{
	if (tryMakeAutomaticActionOfRangedUnit(battle, next))
		return true;

	if (tryMakeAutomaticActionOfMeleeUnit(battle, next))
		return true;

	if (tryMakeAutomaticActionOfCatapult(battle, next))
		return true;

	if (tryMakeAutomaticActionOfFirstAidTent(battle, next))
		return true;

	return false;
}

bool BattleFlowProcessor::tryMakeAutomaticActionOfRangedUnit(const CBattleInfoCallback & battle, const CStack * next) //TODO: optimize readability based on tryMakeAutomaticActionOfMeleeUnit
{
	const CGHeroInstance * curOwner = battle.battleGetOwnerHero(next);
	const CreatureID stackCreatureId = next->unitType()->getId();

	const auto canonicalControl = canonicalWarMachineControl(curOwner, stackCreatureId);
	const bool manualControl = curOwner && (canonicalControl
		? *canonicalControl
		: gameHandler->randomizer->rollCombatAbility(curOwner->id,
			curOwner->valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(stackCreatureId))));
	if (next->hasBonusOfType(BonusType::CPU_CONTROLLED) && (battle.battleCanShoot(next) || !next->isMeleeAttacker())
		&& !manualControl)
	{
		BattleAction attack;
		attack.actionType = EActionType::SHOOT;
		attack.side = next->unitSide();
		attack.stackNumber = next->unitId();

		const TStacks possibleTargets = battle.battleGetStacksIf([&next, &battle](const CStack * s)
		{
			return s->unitOwner() != next->unitOwner() && s->isValidTarget() && battle.battleCanShoot(next, s->getPosition());
		});

		struct TargetInfo
		{
			bool insideTheWalls;
			bool canAttackNextTurn;
			bool isParalyzed;
			bool isMachine;
			double towerAttackValue;
			const CStack * stack;
		};

		const auto & getCanAttackNextTurn = [&battle] (const battle::Unit * unit)
		{
			if (battle.battleCanShoot(unit))
				return true;

			auto units = battle.battleAliveUnits();
			auto availableHexes = battle.battleGetAvailableHexes(unit, true);

			for (const auto * otherUnit : units)
			{
				if (battle.battleCanAttackUnit(unit, otherUnit))
					for (auto position : otherUnit->getHexes())
					{
						if (battle.battleCanAttackHex(availableHexes, unit, position))
							return true;
					}
			}
			return false;
		};

		std::vector<TargetInfo> targetsInfo;

		for (const CStack * possibleTarget : possibleTargets)
		{
			bool isMachine = possibleTarget->unitType()->warMachine != ArtifactID::NONE;
			bool isParalyzed = possibleTarget->hasBonusOfType(BonusType::NOT_ACTIVE) && !isMachine;
			const TargetInfo targetInfo =
			{
				battle.battleIsInsideWalls(possibleTarget->getPosition()),
				getCanAttackNextTurn(possibleTarget),
				isParalyzed,
				isMachine,
				calculateTowerAttackValue(battle, next, possibleTarget),
				possibleTarget
			};
			targetsInfo.push_back(targetInfo);
		}

		const auto & isBetterTarget = [](const TargetInfo & candidate, const TargetInfo & current)
		{
			if (candidate.isParalyzed != current.isParalyzed)
				return candidate.isParalyzed < current.isParalyzed;

			if (candidate.isMachine != current.isMachine)
				return candidate.isMachine < current.isMachine;

			if (candidate.canAttackNextTurn != current.canAttackNextTurn)
				return candidate.canAttackNextTurn > current.canAttackNextTurn;

			if (candidate.insideTheWalls != current.insideTheWalls)
				return candidate.insideTheWalls > current.insideTheWalls;

			return candidate.towerAttackValue > current.towerAttackValue;
		};

		const TargetInfo * target = nullptr;

		for(const auto & elem : targetsInfo)
		{
			if (target == nullptr || isBetterTarget(elem, *target))
				target = &elem;
		}

		if(target == nullptr)
		{
			makeStackDoNothing(battle, next);
		}
		else
		{
			attack.aimToUnit(target->stack);
			makeAutomaticAction(battle, next, attack);
		}
		return true;
	}
	return false;
}

bool BattleFlowProcessor::tryMakeAutomaticActionOfMeleeUnit(const CBattleInfoCallback & battle, const CStack * actingStack)
{
	struct TargetInfo
	{
		bool isMachine = false;
		bool isParalyzed = false;
		bool isReachable = false;
		BattleHex attackableFromHex = BattleHex::INVALID;
		double attackValue = -1.0;
		const CStack * stack = nullptr;

		bool isBetterThan(const TargetInfo & other) const
		{
			if (isParalyzed != other.isParalyzed) return isParalyzed < other.isParalyzed;
			if (isMachine != other.isMachine) return isMachine < other.isMachine;
			if (isReachable != other.isReachable) return isReachable > other.isReachable;
			return attackValue > other.attackValue;
		}
	};

	const CGHeroInstance * curOwner = battle.battleGetOwnerHero(actingStack);
	const CreatureID stackCreatureId = actingStack->unitType()->getId();

	if (!actingStack->hasBonusOfType(BonusType::CPU_CONTROLLED) || battle.battleCanShoot(actingStack))
		return false;

	if (curOwner && gameHandler->randomizer->rollCombatAbility(curOwner->id, curOwner->valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(stackCreatureId))))
		return false;

	ReachabilityInfo reachabilityCache = battle.getReachability(actingStack);

	const TStacks possibleTargets = battle.battleGetStacksIf([&actingStack, &battle](const CStack * s)
	{
		return s->unitOwner() != actingStack->unitOwner() && s->isValidTarget()
		&& (!battle.isEnemyUnitWithinSpecifiedRange(actingStack->position, s, actingStack->getMovementRange())
		|| (battle.isEnemyUnitWithinSpecifiedRange(actingStack->position, s, actingStack->getMovementRange()) && !s->getAttackableHexes(actingStack).empty()));
	});

	TargetInfo bestTarget;

	for (const CStack * possibleTarget : possibleTargets)
	{
		bool isMachine = possibleTarget->unitType()->warMachine != ArtifactID::NONE;
		bool isParalyzed = possibleTarget->hasBonusOfType(BonusType::NOT_ACTIVE) && !isMachine;

		BattleHexArray attackableHexes;
		const auto availableHexes = battle.battleGetAvailableHexes(reachabilityCache, actingStack, true);

		for(const BattleHex & targetHex : possibleTarget->getHexes())
		{
			for(int direction = 0; direction < 8; ++direction)
			{
				auto directionEnum = static_cast<BattleHex::EDir>(direction);
				if(!battle.battleCanAttackHex(availableHexes, actingStack, targetHex, directionEnum))
					continue;

				BattleHex attackFromHex = battle.fromWhichHexAttack(actingStack, targetHex, directionEnum);
				if(availableHexes.contains(attackFromHex))
					attackableHexes.insert(attackFromHex);
			}
		}

		bool isReachable = !attackableHexes.empty();
		if(!isReachable)
			continue;

		BattleHex closestTargetAdjacentHex = std::ranges::min_element(attackableHexes, [&reachabilityCache](const BattleHex & lhs, const BattleHex & rhs)
		{
			return reachabilityCache.distances[lhs.toInt()] < reachabilityCache.distances[rhs.toInt()];
		})[0];

		TargetInfo currentTarget =
		{
			isMachine,
			isParalyzed,
			isReachable,
			closestTargetAdjacentHex,
			calculateTowerAttackValue(battle, actingStack, possibleTarget),
			possibleTarget
		};

		if (!bestTarget.stack || currentTarget.isBetterThan(bestTarget))
			bestTarget = currentTarget;
	}

	if(!bestTarget.stack)
	{
		makeStackDoNothing(battle, actingStack);
	}
	else
	{
		if(bestTarget.isReachable)
		{
			BattleAction meleeAttack = BattleAction::makeMeleeAttack(actingStack, bestTarget.stack, bestTarget.attackableFromHex);
			makeAutomaticAction(battle, actingStack, meleeAttack);
		}
		else if(actingStack->getMovementRange() > 0)
		{
			BattleHex intermediaryHex = battle.getClosestHexToTargetInRange(reachabilityCache, *actingStack, bestTarget.attackableFromHex);
			if(intermediaryHex == BattleHex::INVALID)
			{
				makeStackDoNothing(battle, actingStack);
			}

			BattleAction moveAction = BattleAction::makeMove(actingStack, intermediaryHex);
			makeAutomaticAction(battle, actingStack, moveAction);
		}
		else
		{
			makeStackDoNothing(battle, actingStack);
		}
	}

	return true;
}

bool BattleFlowProcessor::tryMakeAutomaticActionOfCatapult(const CBattleInfoCallback & battle, const CStack * next)
{
	const CGHeroInstance * curOwner = battle.battleGetOwnerHero(next);
	if (next->isCatapult())
	{
		const auto & attackableBattleHexes = battle.getAttackableWallParts();

		if (attackableBattleHexes.empty())
		{
			makeStackDoNothing(battle, next);
			return true;
		}

		const auto canonicalControl = canonicalWarMachineControl(curOwner, CreatureID::CATAPULT);
		const bool manualControl = curOwner && (canonicalControl
			? *canonicalControl
			: gameHandler->randomizer->rollCombatAbility(curOwner->id,
				curOwner->valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(CreatureID(CreatureID::CATAPULT)))));
		if (!manualControl)
		{
			BattleAction attack;
			attack.actionType = EActionType::CATAPULT;
			attack.side = next->unitSide();
			attack.stackNumber = next->unitId();

			makeAutomaticAction(battle, next, attack);
			return true;
		}
	}
	return false;
}

bool BattleFlowProcessor::tryMakeAutomaticActionOfFirstAidTent(const CBattleInfoCallback & battle, const CStack * next)
{
	const CGHeroInstance * curOwner = battle.battleGetOwnerHero(next);
	if (next->isFirstAidTent())
	{
		TStacks possibleStacks = battle.battleGetStacksIf([&next](const CStack * s)
		{
			return s->unitOwner() == next->unitOwner() && s->canBeHealed();
		});

		if (possibleStacks.empty())
		{
			makeStackDoNothing(battle, next);
			return true;
		}

		const auto canonicalControl = canonicalWarMachineControl(curOwner, CreatureID::FIRST_AID_TENT);
		const bool manualControl = curOwner && (canonicalControl
			? *canonicalControl
			: gameHandler->randomizer->rollCombatAbility(curOwner->id,
				curOwner->valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(CreatureID(CreatureID::FIRST_AID_TENT)))));
		if (!manualControl)
		{
			RandomGeneratorUtil::randomShuffle(possibleStacks, gameHandler->getRandomGenerator());
			const CStack * toBeHealed = possibleStacks.front();

			BattleAction heal;
			heal.actionType = EActionType::STACK_HEAL;
			heal.aimToUnit(toBeHealed);
			heal.side = next->unitSide();
			heal.stackNumber = next->unitId();

			makeAutomaticAction(battle, next, heal);
			return true;
		}
	}
	return false;
}

bool BattleFlowProcessor::rollGoodMorale(const CBattleInfoCallback & battle, const CStack * next)
{
	//check for good morale
	auto nextStackMorale = next->moraleVal();
	if(    !next->hadMorale
		&& !next->defending
		&& !next->waited()
		&& !next->fear
		&& next->alive()
		&& next->canMove()
		&& nextStackMorale > 0)
	{
		ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(next->unitSide())->id;
		if (gameHandler->randomizer->rollGoodMorale(ownerArmy, nextStackMorale))
		{
			BattleTriggerEffect bte;
			bte.battleID = battle.getBattle()->getBattleID();
			bte.stackID = next->unitId();
			bte.effect = BonusType::MORALE;
			bte.val = 1;
			bte.additionalInfo = 0;
			gameHandler->sendAndApply(bte); //play animation

			if(const auto * hero = battle.battleGetOwnerHero(next);
				hero && hero->hasActivePerk(
					"new-horizons:discipline",
					"new-horizons:discipline.inspirationalLeader"))
			{
				// The damage script reads melee and ranged percentage boosts
				// separately, so publish both subtypes. STACK_ACTIVATION keeps
				// the bonus through the morale follow-up activation and the
				// normal action-expiry path removes it afterwards.
				Bonus meleeDamage(BonusDuration::STACK_ACTIVATION,
					BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::HERO_SPECIAL, 10,
					BonusSourceID(hero->id), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee));
				meleeDamage.description.appendRawString("New Horizons: Inspirational Leader");
				Bonus rangedDamage(meleeDamage);
				rangedDamage.subtype = BonusCustomSubtype::damageTypeRanged;

				SetStackEffect effect;
				effect.battleID = battle.getBattle()->getBattleID();
				effect.toAdd.emplace_back(next->unitId(), std::vector<Bonus>{
					std::move(meleeDamage), std::move(rangedDamage)});
				gameHandler->sendAndApply(effect);
			}
			return true;
		}
	}
	return false;
}

void BattleFlowProcessor::onActionMade(const CBattleInfoCallback & battle, const BattleAction &ba)
{
	const auto * actedStack = battle.battleGetStackByID(ba.stackNumber, false);
	const auto * activeStack = battle.battleActiveUnit();
	if (ba.actionType == EActionType::END_TACTIC_PHASE)
	{
		onTacticsEnded(battle);
		return;
	}


	//we're after action, all results applied

	// check whether action has ended the battle
	if(owner->checkBattleStateChanges(battle))
		return;

	// tactics - next stack will be selected by player
	if(battle.battleGetTacticDist() != 0)
		return;

	if(ba.timeStopHeroActionPass)
	{
		// This pass consumes only the due Hero Action boundary. If removing that
		// origin released the anchor, return creature control as a continuation;
		// otherwise drain/schedule the remaining Time Stop origins normally.
		if(activeStack && activeStack->alive() && !activeStack->isTimeStopped())
			setActiveStack(battle, activeStack, BattleUnitTurnReason::HERO_COMMAND);
		else
			activateNextStack(battle);
		return;
	}

	// creature will not skip the turn after casting a spell if spell uses canCastWithoutSkip
	if(ba.actionType == EActionType::MONSTER_SPELL)
	{
		assert(activeStack != nullptr);
		assert(actedStack != nullptr);

		// NOTE: in case of random spellcaster, (e.g. Master Genie) spell has been selected by server and was not present in action received from player
		if(actedStack->castSpellThisTurn && ba.spell.hasValue() && ba.spell.toSpell()->canCastWithoutSkip())
		{
			setActiveStack(battle, actedStack, BattleUnitTurnReason::UNIT_SPELLCAST);
			return;
		}
	}

	// Second Wind grants one immediate extra activation to a stack that has
	// already acted. Keep the transient state active while that activation is
	// being processed so its direct-damage penalty is applied authoritatively.
	if(ba.actionType == EActionType::HERO_COMMAND && ba.command == HeroCommand::SECOND_WIND)
	{
		const auto state = battle.battleGetHeroOrderState(ba.side);
		const auto * target = state && state->primaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
			? battle.battleGetStackByID(state->primaryTargetUnitId, false) : nullptr;
		if(const auto * stateInfo = dynamic_cast<const BattleInfo *>(battle.getBattle());
			target && target->alive() && stateInfo
			&& const_cast<BattleInfo *>(stateInfo)->setHeroOrderSecondWindActive(ba.side, true))
		{
			publishHeroOrderState(battle, ba.side);
			setActiveStack(battle, target, BattleUnitTurnReason::HERO_COMMAND);
			// Fire Wall is checked at the start of a genuine Second Wind
			// activation. A lethal trigger must immediately hand flow back to
			// the queue (or finish the battle), rather than leaving a dead stack
			// as the active unit with no request outstanding.
			if(!target->alive())
				activateNextStack(battle);
			return;
		}
	}

	if (ba.isUnitAction())
	{
		assert(activeStack != nullptr);
		assert(actedStack != nullptr);

		if(actedStack->isTimeStopped())
		{
			// The automatic no-op only advances the queue.  Do not grant morale,
			// Orders, or any other second activation to a stopped stack.
			activateNextStack(battle);
			return;
		}

		if(const auto state = battle.battleGetHeroOrderState(actedStack->unitSide());
			state && state->command == HeroCommand::SECOND_WIND && state->secondWindActive
			&& state->primaryTargetUnitId == actedStack->unitId())
		{
			if(const auto * stateInfo = dynamic_cast<const BattleInfo *>(battle.getBattle()))
			{
				const auto side = actedStack->unitSide();
				if(const_cast<BattleInfo *>(stateInfo)->setHeroOrderSecondWindActive(side, false))
					publishHeroOrderState(battle, side);
			}
		}

		if (rollGoodMorale(battle, actedStack))
		{
			// Good morale - same stack makes 2nd turn
			setActiveStack(battle, actedStack, BattleUnitTurnReason::MORALE);
			// A passable Fire Wall can kill the stack before the morale action
			// starts. Continue normal battle flow in that case; otherwise the
			// dead stack would remain active indefinitely.
			if(!actedStack->alive())
				activateNextStack(battle);
			return;
		}
	}
	else
	{
		// A real Hero Action can be the action that creates the all-stopped
		// situation. Remember its side before draining synthetic queue slots;
		// opponent-side Hero Actions while the marker remains active must not
		// retarget the boundary to the wrong player.
		if(activeStack && activeStack->isTimeStopped()
			&& (ba.actionType == EActionType::HERO_SPELL || ba.actionType == EActionType::HERO_COMMAND)
			&& !ba.metamagicDecline)
		{
			if(auto * state = gameHandler->gs->getBattle(battle.getBattle()->getBattleID()))
			{
				const auto markerSide = timeStopMarkerSide(*activeStack);
				const bool isTimeStopCast = ba.actionType == EActionType::HERO_SPELL
					&& ba.spell.hasValue() && ba.spell.toSpell()
					&& ba.spell.toSpell()->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
				const auto side = markerSide.value_or(isTimeStopCast ? ba.side : BattleSide::NONE);
				if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
					state->notePendingTimeStopHeroAction(side);
			}
		}

		if (activeStack && activeStack->alive())
		{
			bool activeStackAffectedBySpell = !activeStack->canMove() ||
				tryActivateBerserkPenalty(battle, battle.battleGetStackByID(battle.getBattle()->getActiveStackID()));

			// this is action made by hero AND unit is neither killed nor affected by reflected spell like blind or berserk
			// keep current active stack for next action
			if (!activeStackAffectedBySpell)
			{
				const auto reason = ba.actionType == EActionType::HERO_COMMAND
					? BattleUnitTurnReason::HERO_COMMAND : BattleUnitTurnReason::HERO_SPELLCAST;
				setActiveStack(battle, activeStack, reason);
				return;
			}
		}
	}

	activateNextStack(battle);
}

bool BattleFlowProcessor::makeStackDoNothing(const CBattleInfoCallback & battle, const CStack * next)
{
	return makeAutomaticAction(battle, next, BattleAction::makeNoAction(next));
}

bool BattleFlowProcessor::makeAutomaticAction(const CBattleInfoCallback & battle, const CStack *stack, const BattleAction &ba)
{
	BattleSetActiveStack bsa;
	bsa.battleID = battle.getBattle()->getBattleID();
	bsa.stack = stack->unitId();
	bsa.reason = BattleUnitTurnReason::AUTOMATIC_ACTION;
	gameHandler->sendAndApply(bsa);
	// Automatic actions still represent a fresh creature activation. Trigger
	// passable Fire Wall footprints after the authoritative nextTurn packet so
	// their activation serial is current and movement callbacks cannot repeat
	// the same damage.
	if(!stack->isTimeStopped() && canonicalFireWallCoversUnit(battle, *stack))
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);
	if(!stack->alive())
		return true;

	bool ret = owner->makeAutomaticBattleAction(battle, ba);
	return ret;
}

void BattleFlowProcessor::removeObstacle(const CBattleInfoCallback & battle, const CObstacleInstance & obstacle)
{
	BattleObstaclesChanged obsRem;
	obsRem.battleID = battle.getBattle()->getBattleID();
	obsRem.change = ObstacleChanges(obstacle.uniqueID, ObstacleChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(obsRem);
}

void BattleFlowProcessor::stackTurnTrigger(const CBattleInfoCallback & battle, const CStack *st)
{
	BattleTriggerEffect bte;
	bte.battleID = battle.getBattle()->getBattleID();
	bte.stackID = st->unitId();
	bte.effect = BonusType::NONE;
	bte.val = 0;
	bte.additionalInfo = 0;
	if (st->alive())
	{
		//unbind
		if (st->hasBonusOfType(BonusType::BIND_EFFECT))
		{
			bool unbind = true;
			BonusList bl = *(st->getBonusesOfType(BonusType::BIND_EFFECT));
			auto adjacent = battle.battleAdjacentUnits(st);

			for (const auto & b : bl)
			{
				if(b->parameters)
				{
					const CStack * stack = battle.battleGetStackByID(b->parameters->toNumber()); //binding stack must be alive and adjacent
					if(stack && vstd::contains(adjacent, stack)) //binding stack is still present
						unbind = false;
				}
				else
				{
					unbind = false;
				}
			}
			if (unbind)
			{
				BattleSetStackProperty ssp;
				ssp.battleID = battle.getBattle()->getBattleID();
				ssp.which = BattleSetStackProperty::UNBIND;
				ssp.stackID = st->unitId();
				gameHandler->sendAndApply(ssp);
			}
		}

		if (st->hasBonusOfType(BonusType::POISON) && !st->waiting)
		{
			std::shared_ptr<const Bonus> b = st->getFirstBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::POISON))).And(Selector::type()(BonusType::STACK_HEALTH)));
			if (b) //TODO: what if not?...
			{
				bte.val = std::max (b->val - 10, -(st->valOfBonuses(BonusType::POISON)));
				if (bte.val < b->val) //(negative) poison effect increases - update it
				{
					bte.effect = BonusType::POISON;
					gameHandler->sendAndApply(bte);
				}
			}
		}
		if(st->hasBonusOfType(BonusType::MANA_DRAIN) && !st->drainedMana)
		{
			const CGHeroInstance * opponentHero = battle.battleGetFightingHero(battle.otherSide(st->unitSide()));
			if(opponentHero)
			{
				ui32 manaDrained = st->valOfBonuses(BonusType::MANA_DRAIN);
				vstd::amin(manaDrained, opponentHero->mana);
				if(manaDrained)
				{
					bte.effect = BonusType::MANA_DRAIN;
					bte.val = manaDrained;
					bte.additionalInfo = opponentHero->id.getNum(); //for sanity
					gameHandler->sendAndApply(bte);
				}
			}
		}
		if (st->hasBonusOfType(BonusType::FEARFUL))
		{
			int chance = st->valOfBonuses(BonusType::FEARFUL);
			ObjectInstanceID opponentArmyID = battle.battleGetArmyObject(battle.otherSide(st->unitSide()))->id;

			if (gameHandler->randomizer->rollCombatAbility(opponentArmyID, chance))
			{
				bte.effect = BonusType::FEARFUL;
				gameHandler->sendAndApply(bte);
			}
		}
		BonusList bl = *(st->getBonuses(Selector::type()(BonusType::ENCHANTER)));
		bl.remove_if([](const Bonus * b)
		{
			return b->subtype.as<SpellID>() == SpellID::NONE;
		});

		BattleSide side = battle.playerToSide(st->unitOwner());
		if(st->canCast())
		{
			bool cast = false;
			while(!bl.empty() && !cast)
			{
				auto bonus = *RandomGeneratorUtil::nextItem(bl, gameHandler->getRandomGenerator());
				auto spellID = bonus->subtype.as<SpellID>();
				const CSpell * spell = SpellID(spellID).toSpell();
				bl.remove_if([&bonus](const Bonus * b)
				{
					return b == bonus.get();
				});

				if (battle.battleGetEnchanterCounter(side) != 0 && bonus->parameters && bonus->parameters->toNumber() != 0)
					continue; // cooldown

				spells::BattleCast parameters(&battle, st, spells::Mode::ENCHANTER, spell);
				parameters.setSpellLevel(bonus->val);

				//todo: recheck effect level
				if(parameters.castIfPossible(gameHandler->spellcastEnvironment(), spells::Target(1, parameters.forceMassive ? spells::Destination() : spells::Destination(st))))
				{
					cast = true;

					int cooldown = bonus->parameters ? bonus->parameters->toNumber() : 0;
					if (cooldown != 0)
					{
						BattleSetStackProperty ssp;
						ssp.battleID = battle.getBattle()->getBattleID();
						ssp.which = BattleSetStackProperty::ENCHANTER_COUNTER;
						ssp.absolute = false;
						ssp.val = cooldown;
						ssp.stackID = st->unitId();
						gameHandler->sendAndApply(ssp);
					}
				}
			}
		}
	}
}

void BattleFlowProcessor::setActiveStack(const CBattleInfoCallback & battle, const battle::Unit * stack, BattleUnitTurnReason reason)
{
	assert(stack);

	BattleSetActiveStack sas;
	sas.battleID = battle.getBattle()->getBattleID();
	sas.stack = stack->unitId();
	sas.reason = reason;
	gameHandler->sendAndApply(sas);
	bool secondWindActivation = false;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto state = battle.battleGetHeroOrderState(stack->unitSide());
		secondWindActivation = state
			&& state->command == HeroCommand::SECOND_WIND
			&& state->secondWindActive
			&& state->primaryTargetUnitId == stack->unitId();
	}
	if(!stack->isTimeStopped()
		&& (reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::MORALE || secondWindActivation)
		&& canonicalFireWallCoversUnit(battle, *stack))
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);
}

double BattleFlowProcessor::calculateTowerAttackValue(const CBattleInfoCallback & battle, const CStack * attacker, const CStack * target) const
{
	double unitValue = target->unitType()->getAIValue();
	double singleHpValue = unitValue / static_cast<double>(target->getMaxHealth());
	double fullHp = static_cast<double>(target->getTotalHealth());

	int distance = BattleHex::getDistance(attacker->getPosition(), target->getPosition());
	BattleAttackInfo attackInfo(attacker, target, distance, attacker->isShooter());
	DamageEstimation estimation = battle.calculateDmgRange(attackInfo);

	double avgDmg = (static_cast<double>(estimation.damage.max) + static_cast<double>(estimation.damage.min)) / 2.0;
	double realAvgDmg = std::min(fullHp, avgDmg);
	double avgUnitKilled = (static_cast<double>(estimation.kills.max) + static_cast<double>(estimation.kills.min)) / 2.0;

	return (realAvgDmg * singleHpValue) + (avgUnitKilled * unitValue);
}
