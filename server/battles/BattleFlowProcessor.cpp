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
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/NewHorizonsHeroicSpirit.h"

#include "BattleProcessor.h"

#include "../CGameHandler.h"
#include "../TurnTimerHandler.h"

#include "../../lib/CStack.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleProxy.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/battle/NewHorizonsConfusionResolution.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusSelector.h"
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
#include "../../lib/spells/NewHorizonsOverwhelmingFormula.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/CObstacleInstance.h"

#include <vstd/RNG.h>

namespace
{
	void publishRapidResponse(CGameHandler * handler, const CBattleInfoCallback & battle,
		BattleSide side, BattleRapidResponseStateChanged::Transition transition)
	{
		BattleRapidResponseStateChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.side = side;
		update.expected = battle.getBattle()->getRapidResponseState(side);
		update.transition = transition;
		update.state = transition == BattleRapidResponseStateChanged::Transition::CAPTURE
			? newHorizonsRapidResponse::capture(battle, side)
			: newHorizonsRapidResponse::resolve(battle, side,
				transition == BattleRapidResponseStateChanged::Transition::CONSUME);
		if(update.state != update.expected)
			handler->sendAndApply(update);
	}

	void publishSwiftLifecycle(CGameHandler * gameHandler, const CBattleInfoCallback & battle,
		const battle::Unit * unit, bool completed)
	{
		if(!unit)
			return;
		const auto next = completed
			? newHorizonsSwiftRebirth::completeNormalActivationPlan(*unit, battle.battleGetRound())
			: newHorizonsSwiftRebirth::reserveNormalActivationPlan(*unit, battle.battleGetRound());
		if(next)
		{
			SetStackEffect update;
			update.battleID = battle.getBattle()->getBattleID();
			update.toUpdate.emplace_back(unit->unitId(), std::vector<Bonus>{*next});
			gameHandler->sendAndApply(update);
		}
	}
	class ProspectiveBattlePlanProxy final : public BattleProxy
	{
	public:
		ProspectiveBattlePlanProxy(Subject subject, BattleSide side, uint32_t anchorStackId,
			PreCombatOrderState state, HeroActionAllowanceState allowances)
			: BattleProxy(std::move(subject)), candidateSide(side), candidateAnchorStackId(anchorStackId),
				candidateState(std::move(state)), candidateAllowances(std::move(allowances))
		{}

		int32_t getActiveStackID() const override
		{
			return static_cast<int32_t>(candidateAnchorStackId);
		}

		BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
		const scripting::Pool & getScriptContextPool() const override { return subject->getBattle()->getScriptContextPool(); }
		std::vector<SpellID> getUsedSpells(BattleSide side) const override
		{
			return subject->getBattle()->getUsedSpells(side);
		}
		uint32_t nextUnitId() const override { return subject->getBattle()->nextUnitId(); }
		int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override
		{
			return subject->getBattle()->getActualDamage(damage, attackerCount, rng);
		}
		int3 getLocation() const override { return subject->getBattle()->getLocation(); }
		BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }

		void nextRound() override { rejectMutation(); }
		void nextTurn(uint32_t, BattleUnitTurnReason) override { rejectMutation(); }
		void addUnit(uint32_t, const JsonNode &) override { rejectMutation(); }
		void updateUnit(uint32_t, const JsonNode &, int64_t) override { rejectMutation(); }
		void moveUnit(uint32_t, const BattleHex &) override { rejectMutation(); }
		void removeUnit(uint32_t) override { rejectMutation(); }
		void addUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
		void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
		void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
		void setWallState(EWallPart, EWallState) override { rejectMutation(); }
		void addObstacle(const ObstacleChanges &) override { rejectMutation(); }
		void updateObstacle(const ObstacleChanges &) override { rejectMutation(); }
		void removeObstacle(uint32_t) override { rejectMutation(); }

		const PreCombatOrderState & getPreCombatOrderState(BattleSide side) const override
		{
			return side == candidateSide ? candidateState : BattleProxy::getPreCombatOrderState(side);
		}

		const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override
		{
			return side == candidateSide ? candidateAllowances : BattleProxy::getHeroActionAllowances(side);
		}

	private:
		[[noreturn]] static void rejectMutation()
		{
			throw std::logic_error("Prospective Battle Plan view is read-only");
		}

		BattleSide candidateSide;
		uint32_t candidateAnchorStackId;
		PreCombatOrderState candidateState;
		HeroActionAllowanceState candidateAllowances;
	};

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
		const auto markers = unit.getBonuses(Selector::type()(BonusType::TIME_STOP));
		for(const auto & marker : *markers)
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

	bool demonicGateFootprintHasObstacle(const CBattleInfoCallback & battle,
		const BattleHex & position, bool doubleWide, BattleSide side)
	{
		for(const auto & hex : battle::Unit::getHexes(position, doubleWide, side))
			if(hex.isAvailable() && !battle.battleGetAllObstaclesOnPos(hex, false).empty())
				return true;
		return false;
	}

	SpellID divineRetributionSpellID()
	{
		static const SpellID id(SpellID::decode("new-horizons:divineRetribution"));
		return id;
	}

	void applyStartOfActivationEffects(CGameHandler * gameHandler,
		const CBattleInfoCallback & battle, const battle::Unit * stack);

	std::optional<BattleSide> quartermasterActiveSide(const CBattleInfoCallback & battle, uint32_t unitId)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(battle.battleGetReducedExtraActivationState(side).activeUnitId == unitId)
				return side;
		return std::nullopt;
	}

	void clearQuartermasterActivation(CGameHandler * gameHandler,
		const CBattleInfoCallback & battle, uint32_t unitId)
	{
		const auto side = quartermasterActiveSide(battle, unitId);
		if(!side)
			return;

		auto state = battle.battleGetReducedExtraActivationState(*side);
		state.activeUnitId = ReducedExtraActivationState::INVALID_UNIT_ID;
		state.outputPercent = 100;
		BattleReducedExtraActivationStateChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.side = *side;
		update.state = state;
		gameHandler->sendAndApply(update);
	}

	void clearUnusableQuartermasterActivations(CGameHandler * gameHandler,
		const CBattleInfoCallback & battle)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto state = battle.battleGetReducedExtraActivationState(side);
			if(!state.hasActiveUnit())
				continue;
			const auto * unit = battle.battleGetStackByID(state.activeUnitId, false);
			if(!unit || !unit->alive() || unit->isGhost() || unit->isTimeStopped())
				clearQuartermasterActivation(gameHandler, battle, state.activeUnitId);
		}
	}

	bool spendQuartermasterAllowance(CGameHandler * gameHandler,
		const CBattleInfoCallback & battle, const battle::Unit * unit)
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTimeStopped()
			|| !(unit->isBallista() || unit->isCatapult() || unit->isFirstAidTent()))
			return false;

		const auto controllerSide = battle.playerToSide(battle.battleGetOwner(unit));
		if(controllerSide != BattleSide::ATTACKER && controllerSide != BattleSide::DEFENDER)
			return false;
		const auto * hero = battle.battleGetOwnerHero(unit);
		if(!hero || !hero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.quartermaster"))
			return false;

		auto state = battle.battleGetReducedExtraActivationState(controllerSide);
		if(!state.enabled || state.used || state.hasActiveUnit())
			return false;

		const auto ammoCarts = battle.battleGetUnitsIf([&battle, unit](const battle::Unit * candidate)
		{
			return candidate && candidate->isAmmoCart() && candidate->alive()
				&& battle.battleMatchOwner(unit, candidate, true);
		});
		if(ammoCarts.empty())
			return false;

		state.used = true;
		state.activeUnitId = unit->unitId();
		state.outputPercent = 50;
		BattleReducedExtraActivationStateChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.side = controllerSide;
		update.state = state;
		gameHandler->sendAndApply(update);

		BattleLogMessage feedback;
		feedback.battleID = update.battleID;
		MetaString line;
		line.appendTextID(hero->getNameTextID());
		line.appendRawString(" uses Quartermaster: %s receives an additional activation at 50% effectiveness.");
		unit->addNameReplacement(line, unit->getCount());
		feedback.lines.push_back(std::move(line));
		gameHandler->sendAndApply(feedback);
		return true;
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
	update.states = battle.getBattle()->getHeroOrderStates(side);
	update.state = update.states->empty()
		? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(update.states->back());
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

	const auto & deployment = battle.getBattle()->getDeploymentState();
	const auto * battleState = dynamic_cast<const IBattleState *>(battle.getBattle());
	if(deployment.independent ? deployment.activeSide() == BattleSide::NONE
		: (!battleState || battleState->getTacticDist() == 0))
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
	if(tryStartPreCombatOrder(battle))
		return;
	activateNextStack(battle);
}

bool BattleFlowProcessor::tryStartPreCombatOrder(const CBattleInfoCallback & battle)
{
	if(!battle.getBattle() || battle.battleGetRound() != 1
		|| battle.getBattle()->getActivationSerial() != 0)
		return false;

	const auto publishState = [this, &battle](BattleSide side, const PreCombatOrderState & next)
	{
		BattleHeroOrderStateChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.side = side;
		update.states = battle.getBattle()->getHeroOrderStates(side);
		if(!update.states->empty())
			update.state = update.states->back();
		update.preCombatOrderState = next;
		gameHandler->sendAndApply(update);
	};

	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & current = battle.getBattle()->getPreCombatOrderState(side);
		if(current.phase != PreCombatOrderState::Phase::AVAILABLE)
			continue;

		const CStack * anchor = nullptr;
		for(const auto * stack : battle.battleGetAllStacks(true))
		{
			if(!stack || !stack->alive() || stack->isGhost() || stack->isTurret()
				|| stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
				|| stack->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
				|| stack->unitSlot() == SlotID::WAR_MACHINES_SLOT
				|| battle.battleGetOwner(stack) != battle.sideToPlayer(side))
				continue;
			if(!anchor || stack->unitId() < anchor->unitId())
				anchor = stack;
		}

		bool hasLegalOrder = false;
		if(anchor)
		{
			auto candidateState = current;
			candidateState.beginOrderRequired(1, anchor->unitId());
			auto candidateAllowances = battle.getBattle()->getHeroActionAllowances(side);
			candidateAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
				HeroActionAllowanceState::GrantSource::BATTLE_PLAN, 1);

			auto subject = std::shared_ptr<CBattleInfoCallback>(
				const_cast<CBattleInfoCallback *>(&battle), [](CBattleInfoCallback *) {});
			ProspectiveBattlePlanProxy prospective(subject, side, anchor->unitId(),
				std::move(candidateState), std::move(candidateAllowances));
			hasLegalOrder = std::any_of(heroCommands::CANONICAL_COMMANDS.begin(),
				heroCommands::CANONICAL_COMMANDS.end(), [&prospective, side](HeroCommand command)
				{
					return prospective.battleCanBeginHeroCommand(side, command);
				});
		}

		if(!anchor || !hasLegalOrder)
		{
			auto completed = current;
			completed.complete();
			publishState(side, completed);

			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString(!anchor
				? "Battle Plan ends: no eligible stack can anchor an opening Order."
				: "Battle Plan ends: no legal opening Order remains.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
			continue;
		}

		auto pending = current;
		pending.beginOrderRequired(1, anchor->unitId());
		publishState(side, pending);
		setActiveStack(battle, anchor, BattleUnitTurnReason::HERO_COMMAND);
		return true;
	}
	return false;
}

void BattleFlowProcessor::startNextRound(const CBattleInfoCallback & battle, bool isFirstRound)
{
	// Swift Gate resolves while the current round still exists. Ordinary Gates
	// resolve only after BattleNextRound advances the authoritative round.
	resolveDemonicGates(battle, true);
	struct SpellPointSnapshot
	{
		const CGHeroInstance * hero;
		int32_t normal;
		int32_t buffer;
	};
	std::vector<SpellPointSnapshot> spellPointSnapshots;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(const auto * hero = battle.battleGetFightingHero(side))
			spellPointSnapshots.push_back({hero, hero->getNormalSpellPoints(), hero->getBufferSpellPoints()});
	if(!isFirstRound)
		resolveDivineRetribution(battle);
	// Timed bonuses are decremented inside BattleNextRound rather than through a
	// SetStackEffect pack. Record only Hydra capacity effects that are about to
	// expire so their compact survivor HP can be clamped immediately afterward.
	static const SpellID hydrasVitalitySpell(SpellID::decode("new-horizons:hydrasVitality"));
	const auto hydrasCapacitySelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::STACK_HEALTH));
	std::vector<uint32_t> expiringHydrasVitalityStacks;
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || unit->isTimeStopped())
			continue;
		const auto capacityBonuses = unit->getBonuses(hydrasCapacitySelector);
		if(capacityBonuses && std::ranges::any_of(*capacityBonuses, [](const auto & bonus)
			{
				return bonus && Bonus::NTurns(bonus.get()) && bonus->turnsRemain == 1;
			}))
			expiringHydrasVitalityStacks.push_back(unit->unitId());
	}
	BattleNextRound bnr;
	bnr.battleID = battle.getBattle()->getBattleID();
	logGlobal->debug("Next round starts");
	gameHandler->sendAndApply(bnr);
	const auto hydrasRegenerationSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::HP_REGENERATION));
	for(const auto stackId : expiringHydrasVitalityStacks)
	{
		const auto * stack = battle.battleGetStackByID(stackId, false);
		if(!stack)
			continue;
		auto state = stack->acquireState();
		if(!state->health.isCapacityHealthTracking())
			continue;
		state->normalizeCapacityHealth();
		const bool capacityRemains = stack->hasBonus(hydrasCapacitySelector);
		const bool regenerationRemains = stack->hasBonus(hydrasRegenerationSelector);
		if(capacityRemains || regenerationRemains)
			continue;
		state->clearCapacityHealthReference();
		UnitChanges update(stackId, UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged normalized;
		normalized.battleID = bnr.battleID;
		normalized.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(normalized);
	}
	BattleLogMessage rewards;
	rewards.battleID = bnr.battleID;
	for(const auto & snapshot : spellPointSnapshots)
	{
		const auto normalRestored = snapshot.hero->getNormalSpellPoints() - snapshot.normal;
		const auto bufferGranted = snapshot.hero->getBufferSpellPoints() - snapshot.buffer;
		if(normalRestored > 0)
		{
			MetaString line = MetaString::createFromTextID(snapshot.hero->getNameTextID());
			line.appendRawString(": Formula Reserve restores ");
			line.appendNumber(normalRestored);
			line.appendRawString(" Normal Spell Points as the Metamagic sequence ends with the round.");
			rewards.lines.push_back(std::move(line));
		}
		if(bufferGranted > 0)
		{
			MetaString line = MetaString::createFromTextID(snapshot.hero->getNameTextID());
			line.appendRawString(": Spell Buffer grants ");
			line.appendNumber(bufferGranted);
			line.appendRawString(" Buffer Spell Points because the unused Metamagic Spell Action expired. Normal Spell Points are unchanged.");
			rewards.lines.push_back(std::move(line));
		}
	}
	if(!rewards.lines.empty())
		gameHandler->sendAndApply(rewards);
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

void BattleFlowProcessor::resolveDivineRetribution(const CBattleInfoCallback & battle)
{
	const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
	const SpellID spell = divineRetributionSpellID();
	if(!concrete || !spell.hasValue())
		return;

	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::DIVINE_RETRIBUTION_JUDGED));
	const auto stacks = concrete->getStacksIf([](const CStack *) { return true; });
	SetStackEffect cleanup;
	cleanup.battleID = battle.getBattle()->getBattleID();

	for(const auto * stack : stacks)
	{
		if(!stack)
			continue;

		const auto judgments = stack->getBonuses(selector);
		if(!judgments || judgments->empty())
			continue;

		std::vector<Bonus> toRemove;
		for(const auto & judgment : *judgments)
		{
			if(!judgment)
				continue;
			toRemove.emplace_back(*judgment);

			int64_t judgmentRound = -1;
			int64_t protectedUnitId = -1;
			int64_t actualHpDamage = 0;
			int64_t rawCap = 0;
			int64_t retributionistPercent = 100;
			if(judgment->parameters)
			{
				try
				{
					const auto parameters = judgment->parameters->toCustom<JsonNode>();
					judgmentRound = parameters["round"].Integer();
					protectedUnitId = parameters["protectedUnitId"].Integer();
					actualHpDamage = parameters["actualHpDamage"].Integer();
					rawCap = parameters["rawCap"].Integer();
					retributionistPercent = parameters["retributionistPercent"].Integer();
				}
				catch(const std::exception &)
				{
					// Malformed saved judgments expire without producing damage.
				}
			}

			if(judgmentRound != battle.battleGetRound() || actualHpDamage <= 0 || rawCap <= 0
				|| protectedUnitId < 0
				|| protectedUnitId > std::numeric_limits<uint32_t>::max())
				continue;
			if(retributionistPercent != 120)
				retributionistPercent = 100;
			if(!stack->alive())
				continue;

			const int64_t thirtyPercent = (actualHpDamage / 100) * 30 + (actualHpDamage % 100) * 30 / 100;
			const int64_t cappedDamage = std::min(thirtyPercent, rawCap);
			const int64_t payout = cappedDamage * retributionistPercent / 100;
			if(payout <= 0)
				continue;

			BattleStackAttacked hit;
			const auto protectedId = static_cast<uint32_t>(protectedUnitId);
			hit.attackerID = protectedId;
			hit.stackAttacked = stack->unitId();
			hit.damageAmount = payout;
			hit.flags = BattleStackAttacked::SPELL_EFFECT;
			hit.spellID = spell;
			CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), stack->acquireState(),
				false, false, battle::DamageProvenance::SPELL);
			if(hit.damageAmount <= 0)
				continue;

			StacksInjured injury;
			injury.battleID = battle.getBattle()->getBattleID();
			injury.stacks.push_back(hit);
			gameHandler->sendAndApply(injury);

			BattleLogMessage log;
			log.battleID = injury.battleID;
			MetaString line;
			line.appendRawString("Divine Retribution deals ");
			line.appendNumber(hit.damageAmount);
			line.appendRawString(" Holy damage to %s.");
			stack->addNameReplacement(line, stack->getCount());
			log.lines.push_back(std::move(line));
			gameHandler->sendAndApply(log);
		}

		if(!toRemove.empty())
			cleanup.toRemove.emplace_back(stack->unitId(), std::move(toRemove));
	}

	if(!cleanup.toRemove.empty())
		gameHandler->sendAndApply(cleanup);
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
		const bool reinforcedGate = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.reinforcedGate");
		const bool infernalBeacon = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.infernalBeacon");
		const bool reserveDiscipline = hero && hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.reserveDiscipline");

		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = sideId;
		update.reserve = snapshot.demonicReserve;
		update.gated = snapshot.gatedDemonicStacks;
		update.chainGateArmed = snapshot.chainGateArmed;
		update.masterGateUsed = snapshot.masterGateUsed;
		std::vector<uint32_t> hellfireSources;

		for(const auto & gate : snapshot.pendingDemonicGates)
		{
			const bool due = endOfRoundPhase
				? gate.chainGateAccelerated || gate.arrivalRound <= concrete->getRound()
					|| (swiftGate && gate.arrivalRound <= concrete->getRound() + 1)
				: gate.arrivalRound <= concrete->getRound();
			if(!due)
			{
				update.pending.push_back(gate);
				continue;
			}

			if(!gate.creature.hasValue())
			{
				update.pending.push_back(gate);
				continue;
			}
			const auto * creature = gate.creature.toCreature();
			if(!creature || gate.count <= 0)
			{
				update.pending.push_back(gate);
				continue;
			}
			auto accessibility = battle.getAccessibility();
			const BattleHex arrival = gate.position;
			if(!accessibility.accessibleForDemonicGateArrival(arrival, creature->isDoubleWide(), sideId,
				gate.position, creature->isDoubleWide())
				|| demonicGateFootprintHasObstacle(battle, arrival, creature->isDoubleWide(), sideId))
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

			const auto devilId = CreatureID(CreatureID::decode("core:devil"));
			if(devilId.hasValue())
			{
				const auto * devil = devilId.toCreature();
				if(devil && !devil->sounds.startMoving.empty())
				{
					BattleAnimationPlayed arrivalSound;
					arrivalSound.battleID = concrete->getBattleID();
					arrivalSound.sound = devil->sounds.startMoving;
					arrivalSound.targets.push_back({static_cast<int32_t>(info.id), arrival});
					gameHandler->sendAndApply(arrivalSound);
				}
			}

			update.gated.push_back({info.id, gate.creature, gate.count});
			const auto * gated = battle.battleGetStackByID(info.id, false);
			int64_t reinforcedHealth = 0;
			if(reinforcedGate && gated)
			{
				auto state = gated->acquireState();
				reinforcedHealth = state->getAvailableHealth() * 20 / 100;
				state->health.addTemporaryHitPoints(reinforcedHealth);
				BattleUnitsChanged reinforce;
				reinforce.battleID = concrete->getBattleID();
				UnitChanges changed(info.id, UnitChanges::EOperation::UPDATE);
				changed.data = state->save();
				reinforce.changedStacks.push_back(std::move(changed));
				gameHandler->sendAndApply(reinforce);
				gated = battle.battleGetStackByID(info.id, false);
			}

			std::vector<Bonus> arrivalBonuses;
			bool infernalBeaconActivated = false;
			bool reserveDisciplineActivated = false;
			if(infernalBeacon && gated)
			{
				const bool adjacentInfernoAlly = std::ranges::any_of(battle.battleAdjacentUnits(gated), [&battle, sideId, info](const auto * adjacent)
				{
					return adjacent && adjacent->alive() && adjacent->unitId() != info.id
						&& battle.playerToSide(battle.battleGetOwner(adjacent)) == sideId
						&& adjacent->creatureId().toCreature()->getFactionID() == FactionID::INFERNO;
				});
				if(adjacentInfernoAlly)
				{
					infernalBeaconActivated = true;
					Bonus initiative(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE_FLAT,
						BonusSource::HERO_SPECIAL, 2, BonusSourceID(hero->id));
					initiative.turnsRemain = endOfRoundPhase ? 2 : 1;
					initiative.description.appendRawString("Infernal Beacon");
					arrivalBonuses.push_back(std::move(initiative));
				}
			}
			if(reserveDiscipline && gated)
			{
				reserveDisciplineActivated = true;
				Bonus moraleFloor(BonusDuration::N_TURNS, BonusType::MINIMUM_MORALE,
					BonusSource::HERO_SPECIAL, 0, BonusSourceID(hero->id));
				moraleFloor.turnsRemain = endOfRoundPhase ? 2 : 1;
				moraleFloor.description.appendRawString("Reserve Discipline");
				arrivalBonuses.push_back(std::move(moraleFloor));
			}
			if(!arrivalBonuses.empty())
			{
				SetStackEffect effect;
				effect.battleID = concrete->getBattleID();
				effect.toAdd.emplace_back(info.id, std::move(arrivalBonuses));
				gameHandler->sendAndApply(effect);
			}

			if(hellfireArrival)
				hellfireSources.push_back(info.id);
			BattleLogMessage message;
			message.battleID = concrete->getBattleID();
			const char * gateName = gate.chainGateAccelerated
				? "Chain Gate"
				: endOfRoundPhase ? "Swift Gate" : "Ordinary Gate";
			MetaString line = MetaString::createFromRawString(gateName);
			line.appendRawString(" brings forth ");
			line.appendNumber(gate.count);
			line.appendRawString(" ");
			line.appendName(gate.creature, gate.count);
			line.appendRawString(".");
			if(reinforcedHealth > 0)
			{
				line.appendRawString(" Reinforced Gate grants ");
				line.appendNumber(reinforcedHealth);
				line.appendRawString(" temporary Health.");
			}
			if(infernalBeaconActivated)
				line.appendRawString(" Infernal Beacon grants +2 Initiative.");
			if(reserveDisciplineActivated)
				line.appendRawString(" Reserve Discipline prevents negative Morale.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		gameHandler->sendAndApply(update);

		// Publish the gated-stack identity before Hellfire emits its indirect
		// damage packet.  That packet carries the gated unit as attackerID, so
		// the shared visitor can authoritatively recognize a lethal Hellfire hit
		// as a Chain Gate trigger.
		for(const auto sourceId : hellfireSources)
		{
			const auto * gated = battle.battleGetStackByID(sourceId, false);
			if(!gated)
				continue;
			std::vector<const battle::Unit *> enemies;
			for(const auto * adjacent : battle.battleAdjacentUnits(gated))
				if(adjacent && adjacent->alive() && adjacent->unitSide() != sideId)
					enemies.push_back(adjacent);
			const int64_t totalFireDamage = gated->getAvailableHealth() * 15 / 100;
			const int64_t damagePerEnemy = enemies.empty() ? 0 : totalFireDamage / enemies.size();
			if(damagePerEnemy <= 0)
				continue;

			StacksInjured injury;
			injury.battleID = concrete->getBattleID();
			BattleLogMessage hellfireLog;
			hellfireLog.battleID = concrete->getBattleID();
			for(const auto * enemy : enemies)
			{
				BattleStackAttacked hit;
				hit.attackerID = sourceId;
				hit.stackAttacked = enemy->unitId();
				hit.damageAmount = damagePerEnemy;
				hit.flags |= BattleStackAttacked::SPELL_EFFECT;
				CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), enemy->acquireState(),
					false, false, battle::DamageProvenance::SPELL);
				MetaString hellfireLine = MetaString::createFromRawString("Hellfire from ");
				hellfireLine.appendNumber(gated->getCount());
				hellfireLine.appendRawString(" ");
				hellfireLine.appendName(gated->creatureId(), gated->getCount());
				hellfireLine.appendRawString(" hits ");
				hellfireLine.appendNumber(enemy->getCount());
				hellfireLine.appendRawString(" ");
				hellfireLine.appendName(enemy->creatureId(), enemy->getCount());
				hellfireLine.appendRawString(" for ");
				hellfireLine.appendNumber(hit.damageAmount);
				hellfireLine.appendRawString(" damage, killing ");
				hellfireLine.appendNumber(hit.killedAmount);
				hellfireLine.appendRawString(".");
				hellfireLog.lines.push_back(std::move(hellfireLine));
				injury.stacks.push_back(hit);
			}
			gameHandler->sendAndApply(injury);
			gameHandler->sendAndApply(hellfireLog);
		}
	}
}

const CStack * BattleFlowProcessor::getNextStack(const CBattleInfoCallback & battle)
{
	// This is reached only after immediate extra/continuation routing has drained.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle.getBattle()->getRapidResponseState(side).pending()
			&& !newHorizonsRapidResponse::pendingWaiter(battle, side))
			publishRapidResponse(gameHandler, battle, side, BattleRapidResponseStateChanged::Transition::CLEAR);
	std::vector<battle::Units> q;
	battle.battleGetTurnOrder(q, 1, 0, -1); //todo: get rid of "turn -1"

	if(q.empty())
		return nullptr;

	if(q.front().empty())
		return nullptr;

	const auto * next = q.front().front();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(newHorizonsRapidResponse::pendingWaiter(battle, side) == next)
			publishRapidResponse(gameHandler, battle, side, BattleRapidResponseStateChanged::Transition::CONSUME);
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

	if(!next || (!next->willMove() && !(next->isTimeStopped() && !next->timeStopTurnConsumed())
		&& !(newHorizonsFrozen::isFrozen(*next) && !next->moved() && !next->defended())))
		return nullptr;

	return stack;
}

void BattleFlowProcessor::activateNextStack(const CBattleInfoCallback & battle)
{
	// Find next stack that requires manual control
	for (;;)
	{
		// A reduced extra activation can be waiting behind other queue entries.
		// Reconcile only the two saved active identities at queue boundaries so
		// an absent/dead/ghost/stopped machine cannot leave a stale 50% output
		// marker, while a live waiting machine keeps its earned activation.
		clearUnusableQuartermasterActivations(gameHandler, battle);

		// battle has ended
		if (owner->checkBattleStateChanges(battle))
			return;
		if(battle.getBattle()->getCrisisCommandState().choice()) return;
		if(tryStartCrisisCommand(battle)) return;
		if(!battle.getBattle()->getCrisisCommandState().returns.empty())
		{
			resumeCrisisCommand(battle);
			return;
		}

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
		publishSwiftLifecycle(gameHandler, battle, next, true);
		return makeStackDoNothing(battle, next);
	}
	if(newHorizonsFrozen::forfeitsNormalActivation(*next, BattleUnitTurnReason::TURN_QUEUE))
	{
		publishSwiftLifecycle(gameHandler, battle, next, true);
		return makeStackDoNothing(battle, next);
	}

	if(tryActivateMoralePenalty(battle, next))
		return true;

	bool turnTriggersProcessed = false;
	if(tryActivateConfusion(battle, next, turnTriggersProcessed))
		return true;

	if(tryActivateBerserkPenalty(battle, next))
		return true;

	if(handleForcedCpuControlledUnit(battle, next))
		return true;

	if(!turnTriggersProcessed)
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
	int nextStackMorale = battle.battleGetMorale(next);
	if(!next->hadMorale && !next->waited() && nextStackMorale < 0)
	{
		ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(next->unitSide())->id;
		const auto affectedSide = battle.playerToSide(battle.battleGetOwner(next));
		const auto drawBadMorale = [this, ownerArmy, nextStackMorale]()
		{
			return gameHandler->randomizer->rollBadMorale(ownerArmy, -nextStackMorale);
		};
		const bool firstBadMorale = drawBadMorale();
		if(affectedSide == BattleSide::ATTACKER || affectedSide == BattleSide::DEFENDER)
		{
			auto suppression = battle.getBattle()->getMoraleSuppressionState(affectedSide);
			const bool unbreakableAvailable = suppression.roundAvailable();
			if(suppression.consume(firstBadMorale))
			{
				BattleMoraleSuppressionStateChanged changed;
				changed.battleID = battle.getBattle()->getBattleID();
				changed.side = affectedSide;
				changed.state = suppression;
				gameHandler->sendAndApply(changed);

				BattleLogMessage feedback;
				feedback.battleID = changed.battleID;
				MetaString line;
				if(const auto * hero = battle.getBattle()->getSideHero(affectedSide))
				{
					line.appendTextID(hero->getNameTextID());
					line.appendRawString(": ");
				}
				if(unbreakableAvailable)
					line.appendRawString("Unbreakable ignores the first negative Morale trigger this round. The stack acts normally.");
				else
					line.appendRawString("Rally cancels the first negative Morale trigger this combat. The stack acts normally.");
				feedback.lines.push_back(std::move(line));
				gameHandler->sendAndApply(feedback);
				return false;
			}
		}
		// Preserve the original first draw for Twist; only its final reroll draws again.
		auto cachedDraw = [drawBadMorale, firstBadMorale, firstDraw = true]() mutable
		{
			if(firstDraw)
			{
				firstDraw = false;
				return firstBadMorale;
			}
			return drawBadMorale();
		};
		const bool badMorale = owner->resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), affectedSide,
			gameHandler->randomizer->isBadMoraleRollStochastic(-nextStackMorale), true, cachedDraw);
		if(badMorale)
		{
			//unit loses its turn - empty freeze action
			BattleAction ba;
			ba.actionType = EActionType::BAD_MORALE;
			ba.side = next->unitSide();
			ba.stackNumber = next->unitId();

			std::vector<Bonus> pendingConfusion;
			const auto markers = next->getBonusesOfType(BonusType::CONFUSION_PENDING);
			if(markers)
				for(const auto & marker : *markers)
					if(newHorizonsConfusionControl::isPendingMarker(marker.get()))
						pendingConfusion.emplace_back(*marker);

			if(!makeAutomaticAction(battle, next, ba))
				return false;
			if(!pendingConfusion.empty())
			{
				SetStackEffect effect;
				effect.battleID = battle.getBattle()->getBattleID();
				effect.toRemove.emplace_back(ba.stackNumber, std::move(pendingConfusion));
				gameHandler->sendAndApply(effect);
			}
			return true;
		}
	}
	return false;
}

bool BattleFlowProcessor::tryActivateConfusion(const CBattleInfoCallback & battle, const CStack * next,
	bool & turnTriggersProcessed)
{
	if(!next || !battle.getBattle() || !next->confusionState.pending)
		return false;
	std::vector<Bonus> markers;
	for(const auto & marker : *next->getBonusesOfType(BonusType::CONFUSION_PENDING))
		if(newHorizonsConfusionControl::isPendingMarker(marker.get()))
			markers.emplace_back(*marker);
	if(markers.empty())
		return false;

	// Confusion replaces tactical choice, not the stack's ordinary turn
	// triggers. Unbind/Enchanter may change legality or remove pending state.
	stackTurnTrigger(battle, next);
	turnTriggersProcessed = true;
	if(!next->confusionState.pending)
		return false;
	const auto battleID = battle.getBattle()->getBattleID();
	const auto unitID = next->unitId();
	const auto consumePending = [&]()
	{
		std::vector<Bonus> currentMarkers;
		for(const auto & marker : *next->getBonusesOfType(BonusType::CONFUSION_PENDING))
			if(newHorizonsConfusionControl::isPendingMarker(marker.get()))
				currentMarkers.emplace_back(*marker);
		if(currentMarkers.empty())
			currentMarkers = markers; // Retain exact source identity through startup death.
		if(!currentMarkers.empty())
		{
			SetStackEffect remove;
			remove.battleID = battleID;
			remove.toRemove.emplace_back(unitID, std::move(currentMarkers));
			gameHandler->sendAndApply(remove);
		}
		// Startup death can remove/hide the bonus before this consumer runs.
		// The saved pending state still must be cleared on the dead stack.
		if(next->confusionState.pending)
		{
			auto spentState = next->acquireState();
			spentState->confusionState.clearPending();
			BattleUnitsChanged spent;
			spent.battleID = battleID;
			UnitChanges update(unitID, UnitChanges::EOperation::UPDATE);
			update.data = spentState->save();
			spent.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(spent);
		}
	};
	if(next->fear)
	{
		// An ordinary Fear forfeiture consumes this next activation without
		// manufacturing an Attack/Defend/Wander history entry.
		consumePending();
		return makeStackDoNothing(battle, next);
	}
	if(!beginAutomaticActivation(battle, next))
	{
		consumePending();
		return true;
	}
	// Expiring activation bonuses and start-of-activation damage have now
	// resolved. Choose only from the surviving stack's current legal actions.
	const auto outcomes = newHorizonsConfusion::enumerateOutcomes(battle, next,
		next->confusionState.previousResolved, next->confusionState.pendingConfounder);
	const auto outcome = newHorizonsConfusion::selectOutcome(outcomes,
		gameHandler->getRandomGenerator().nextDouble(1.0));
	consumePending();
	auto state = next->acquireState();
	state->confusionState.recordResolved(outcome.behavior);
	BattleUnitsChanged record;
	record.battleID = battleID;
	UnitChanges update(unitID, UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	record.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(record);

	BattleLogMessage message;
	message.battleID = battleID;
	MetaString line;
	line.appendRawString("Confusion makes %s ");
	next->addNameReplacement(line);
	line.appendRawString(outcome.behavior == battle::ConfusionBehavior::ATTACK ? "Attack."
		: outcome.behavior == battle::ConfusionBehavior::WANDER ? "Wander." : "Defend.");
	message.lines.push_back(std::move(line));
	gameHandler->sendAndApply(message);

	BattleAction action;
	action.side = next->unitSide();
	action.stackNumber = unitID;
	action.actionType = outcome.action.skirmisher ? EActionType::WALK_AND_ATTACK : outcome.action.type;
	if(outcome.action.type == EActionType::WALK || outcome.action.type == EActionType::WALK_AND_ATTACK
		|| outcome.action.skirmisher)
		action.aimToHex(outcome.action.position);
	if(outcome.action.target)
		action.aimToUnit(outcome.action.target);
	action.archerySkirmisherAttack = outcome.action.skirmisher;
	return owner->makeAutomaticBattleAction(battle, action);
}

bool BattleFlowProcessor::tryActivateBerserkPenalty(const CBattleInfoCallback & battle, const CStack * next)
{
	if(!next || !battle.getBattle())
		return false;

	if(newHorizonsPuppetMaster::hasValidControlMarker(battle, next))
		return false;

	if (next->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)) //while in berserk
	{
		const auto battleID = battle.getBattle()->getBattleID();
		const auto unitID = next->unitId();
		const auto unitSide = next->unitSide();
		const auto forcedActivationSpeed = newHorizonsBerserk::forcedActivationSpeedBonus(battle, next);
		const auto removeCompletedBerserkActivation = [this, battleID, unitID]()
		{
			const auto * currentBattle = gameHandler->gameState().getBattle(battleID);
			const auto * currentBerserker = currentBattle
				? currentBattle->battleGetStackByID(unitID, false) : nullptr;
			if(!currentBattle || !currentBerserker)
				return;

			auto bonuses = newHorizonsBerserk::completedForcedActivationBonuses(*currentBattle,
				currentBerserker);
			if(bonuses.empty())
				return;

			SetStackEffect remove;
			remove.battleID = battleID;
			remove.toRemove.emplace_back(unitID, std::move(bonuses));
			gameHandler->sendAndApply(remove);
		};
		const auto removeFrenziedCurseBonus = [this, battleID, unitID]()
		{
			const auto * currentBattle = gameHandler->gameState().getBattle(battleID);
			const auto * currentUnit = currentBattle ? currentBattle->battleGetStackByID(unitID, false) : nullptr;
			if(!currentUnit)
				return;

			const auto bonuses = currentUnit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
				BonusSourceID(SpellID(SpellID::BERSERK))).And(Selector::type()(BonusType::STACKS_SPEED)));
			if(!bonuses)
				return;

			std::vector<Bonus> toRemove;
			for(const auto & bonus : *bonuses)
				if(bonus && newHorizonsBerserk::isFrenziedCurseSpeedBonus(bonus.get()))
					toRemove.push_back(*bonus);
			if(toRemove.empty())
				return;

			SetStackEffect remove;
			remove.battleID = battleID;
			remove.toRemove.emplace_back(unitID, std::move(toRemove));
			gameHandler->sendAndApply(remove);
		};
		const auto cleanupFrenziedCurseBonus = vstd::makeScopeGuard([removeFrenziedCurseBonus]()
		{
			removeFrenziedCurseBonus();
		});

		const auto existingSpeedBonuses = next->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::BERSERK))).And(Selector::type()(BonusType::STACKS_SPEED)));
		const bool hasFrenziedCurseBonus = existingSpeedBonuses
			&& std::ranges::any_of(*existingSpeedBonuses, [](const auto & bonus)
			{
				return bonus && newHorizonsBerserk::isFrenziedCurseSpeedBonus(bonus.get());
			});

		if(!forcedActivationSpeed && hasFrenziedCurseBonus)
			removeFrenziedCurseBonus();

		bool grantedFrenziedCurseBonus = false;
		if(forcedActivationSpeed && !hasFrenziedCurseBonus)
		{
			SetStackEffect add;
			add.battleID = battleID;
			add.toAdd.emplace_back(unitID, std::vector<Bonus>{*forcedActivationSpeed});
			gameHandler->sendAndApply(add);
			grantedFrenziedCurseBonus = true;
		}

		const auto * currentBattle = gameHandler->gameState().getBattle(battleID);
		const auto * currentBerserker = currentBattle ? currentBattle->battleGetStackByID(unitID, false) : nullptr;
		if(!currentBerserker)
			return true;

		const auto candidates = battle.getBerserkForcedActions(currentBerserker);
		// Inspection and AI projection enumerate ties without consuming RNG.
		// Only the authoritative activation chooses the actual target.
		const ForcedAction forcedAction = candidates.size() > 1
			? *RandomGeneratorUtil::nextItem(candidates, gameHandler->getRandomGenerator())
			: candidates.empty() ? ForcedAction{} : candidates.front();
		if(forcedAction.type == EActionType::NO_ACTION)
		{
			removeFrenziedCurseBonus();
			if(makeStackDoNothing(battle, currentBerserker))
				removeCompletedBerserkActivation();
			return true;
		}
		if(grantedFrenziedCurseBonus)
		{
			BattleLogMessage message;
			message.battleID = battleID;
			MetaString line;
			line.appendRawString("Frenzied Curse gives %s +2 Speed for its forced activation.");
			currentBerserker->addNameReplacement(line);
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		if (forcedAction.type == EActionType::SHOOT)
		{
			BattleAction rangeAttack;
			rangeAttack.actionType = EActionType::SHOOT;
			rangeAttack.side = unitSide;
			rangeAttack.stackNumber = unitID;
			rangeAttack.aimToUnit(forcedAction.target);
			if(makeAutomaticAction(battle, currentBerserker, rangeAttack))
				removeCompletedBerserkActivation();
		}
		else if (forcedAction.type == EActionType::WALK_AND_ATTACK)
		{
			BattleAction meleeAttack;
			meleeAttack.actionType = EActionType::WALK_AND_ATTACK;
			meleeAttack.side = unitSide;
			meleeAttack.stackNumber = unitID;
			meleeAttack.aimToHex(forcedAction.position);
			meleeAttack.aimToUnit(forcedAction.target);
			if(makeAutomaticAction(battle, currentBerserker, meleeAttack))
				removeCompletedBerserkActivation();
		} else if (forcedAction.type == EActionType::WALK)
		{
			BattleAction movement;
			movement.actionType = EActionType::WALK;
			movement.side = unitSide;
			movement.stackNumber = unitID;
			movement.aimToHex(forcedAction.position);
			if(makeAutomaticAction(battle, currentBerserker, movement))
				removeCompletedBerserkActivation();
		}
		else
		{
			removeFrenziedCurseBonus();
			if(makeStackDoNothing(battle, currentBerserker))
				removeCompletedBerserkActivation();
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

	const bool fortificationEngineerControl = battle.battleCanUseFortificationEngineer(next);
	const auto canonicalControl = fortificationEngineerControl
		? std::optional<bool>(true)
		: canonicalWarMachineControl(curOwner, stackCreatureId);
	const bool ordinaryManualControl = curOwner && (canonicalControl
		? *canonicalControl
		: gameHandler->randomizer->rollCombatAbility(curOwner->id,
			curOwner->valOfBonuses(BonusType::MANUAL_CONTROL, BonusSubtypeID(stackCreatureId))));
	const bool counterBatteryControl = next->isTurret() && battle.battleHasCounterBatteryMachineTarget(next);
	const bool manualControl = ordinaryManualControl || counterBatteryControl;
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
		TStacks possibleStacks = battle.battleGetStacksIf([&battle, &next](const CStack * s)
		{
			return battle.battleCanHealWithFirstAidTent(next, s);
		});

		std::vector<EWallPart> repairableParts;
		for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
		{
			const auto part = static_cast<EWallPart>(i);
			if(battle.battleGetFirstAidStructureRepairPreview(next, part).repairedHP() > 0)
				repairableParts.push_back(part);
		}
		if (possibleStacks.empty() && repairableParts.empty())
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
			if(possibleStacks.empty())
			{
				BattleAction repair;
				repair.actionType = EActionType::STACK_HEAL;
				repair.side = next->unitSide();
				repair.stackNumber = next->unitId();
				repair.aimToHex(battle.wallPartToBattleHex(repairableParts.front()));
				makeAutomaticAction(battle, next, repair);
				return true;
			}
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
	if(newHorizonsSwiftRebirth::blocksAdditionalActivation(*next, battle.battleGetRound()))
		return false; // Check the birth-round cap before drawing Morale RNG.
	//check for good morale
	auto nextStackMorale = battle.battleGetMorale(next);
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
			bte.heroicSpiritGrant = newHorizonsHeroicSpirit::canGrantEarnedMorale(*next,
				battle.battleGetOwnerHero(next));
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

namespace
{
SpellID plagueRuntimeSpellId()
{
	static const SpellID id(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	return id;
}

std::shared_ptr<const Bonus> plagueStatusBonus(const battle::Unit * unit)
{
	if(!unit)
		return {};
	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(plagueRuntimeSpellId())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
	return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
}

void applyPlagueEndOfActivation(CGameHandler * gameHandler, const CBattleInfoCallback & battle, const CStack * stack)
{
	if(!gameHandler || !stack)
		return;
	const auto marker = plagueStatusBonus(stack);
	if(!marker)
		return; // Dispel or natural completion has removed the authoritative status.

	BattleSide casterSide = stack->unitSide();
	int32_t spreadAttempts = 0;
	int32_t lastProcessedRound = -1;
	int32_t sourceUnitId = -1;
	JsonNode parameters;
	if(marker->parameters)
	{
		try
		{
			parameters = marker->parameters->toCustom<JsonNode>();
			if(parameters.isStruct())
			{
				if(parameters["casterSide"].isNumber())
					casterSide = static_cast<BattleSide>(parameters["casterSide"].Integer());
				if(parameters["spreadAttempts"].isNumber())
					spreadAttempts = std::max<int32_t>(0, parameters["spreadAttempts"].Integer());
				if(parameters["lastProcessedRound"].isNumber())
					lastProcessedRound = parameters["lastProcessedRound"].Integer();
				if(parameters["sourceUnitId"].isNumber())
					sourceUnitId = parameters["sourceUnitId"].Integer();
			}
		}
		catch(const std::exception &)
		{
			// A legacy or malformed marker retains the safe defaults above.
		}
	}
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		casterSide = stack->unitSide();
	const auto currentRound = battle.battleGetRound();
	if(lastProcessedRound >= currentRound)
		return; // Morale/Second Wind and other extra activations still tick once per round.

	const auto rawDamage = std::max<int64_t>(0, marker->val);
	const auto adjustedDamage = newHorizonsPlague::adjustedTickDamage(
		battle, casterSide, stack, rawDamage, &parameters["mdrPenetration"]);
	int64_t actualDamage = 0;
	if(stack->alive() && adjustedDamage > 0)
	{
		auto state = stack->acquireState();
		BattleStackAttacked hit;
		const auto * source = sourceUnitId >= 0 ? battle.battleGetUnitByID(static_cast<uint32_t>(sourceUnitId)) : nullptr;
		if(!source || source->unitSide() != casterSide)
			for(const auto * candidate : battle.battleGetAllStacks(true))
				if(candidate && candidate->unitSide() == casterSide)
				{
					source = candidate;
					break;
				}
		hit.attackerID = source ? source->unitId() : stack->unitId();
		hit.stackAttacked = stack->unitId();
		hit.damageAmount = adjustedDamage;
		hit.flags = BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = plagueRuntimeSpellId();
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state,
			false, false, battle::DamageProvenance::SPELL);
		actualDamage = hit.damageAmount;
		StacksInjured injury;
		injury.battleID = battle.getBattle()->getBattleID();
		injury.stacks.push_back(hit);
		if(auto claim = spells::overwhelmingFormulaClaim(battle, plagueRuntimeSpellId().toSpell(),
			parameters["mdrPenetration"], injury.stacks))
			gameHandler->sendAndApply(*claim);
		gameHandler->sendAndApply(injury);
	}

	BattleLogMessage tickLog;
	tickLog.battleID = battle.getBattle()->getBattleID();
	MetaString tickLine;
	tickLine.appendRawString("Plague deals ");
	tickLine.appendNumber(actualDamage);
	tickLine.appendRawString(" magical damage to %s.");
	stack->addNameReplacement(tickLine, stack->getCount());
	tickLog.lines.push_back(std::move(tickLine));
	gameHandler->sendAndApply(tickLog);

	// Damage precedes each tick's captured propagation allowance. A lethal
	// final tick still propagates before its source status is discarded.
	const auto propagationLimit = newHorizonsPlague::capturedPropagationLimit(*marker);
	for(int32_t recipient = 0; recipient < propagationLimit; ++recipient)
	{
		const auto spreadTargetId = newHorizonsPlague::selectNextSpreadTarget(battle, stack,
			[&battle, casterSide](const battle::Unit * candidate)
			{
				return newHorizonsPlague::isSpreadRecipientReceptive(battle, casterSide, candidate);
			});
		if(!spreadTargetId) break;
		if(spreadTargetId)
		{
			const auto * spreadTarget = battle.battleGetUnitByID(*spreadTargetId);
			if(spreadTarget)
			{
				Bonus infection(*marker);
				infection.turnsRemain = 3;
				const auto infectionRound = battle.battleGetRound();
				if(infection.parameters)
				{
					try
					{
						JsonNode spreadParameters = infection.parameters->toCustom<JsonNode>();
						spreadParameters["spreadAttempts"].Integer() = 0;
						spreadParameters["lastProcessedRound"].Integer() = infectionRound - 1;
						infection.parameters = std::make_shared<BonusParameters>(spreadParameters);
					}
					catch(const std::exception &)
					{
						// Keep inherited parameters when loading a malformed legacy marker.
					}
				}
				SetStackEffect addInfection;
				addInfection.battleID = battle.getBattle()->getBattleID();
				addInfection.toAdd.emplace_back(spreadTarget->unitId(), std::vector<Bonus>{infection});
				gameHandler->sendAndApply(addInfection);
				if(!newHorizonsPlague::hasPlague(battle.battleGetUnitByID(*spreadTargetId)))
					break; // Advance only after the authoritative infection is present.

				BattleLogMessage spreadLog;
				spreadLog.battleID = battle.getBattle()->getBattleID();
				MetaString spreadLine;
				spreadLine.appendRawString(casterSide == spreadTarget->unitSide()
					? "Plague spreads to a friendly stack, %s."
					: "Plague spreads to an enemy stack, %s.");
				spreadTarget->addNameReplacement(spreadLine, spreadTarget->getCount());
				spreadLog.lines.push_back(std::move(spreadLine));
				gameHandler->sendAndApply(spreadLog);
			}
		}

	}
	SetStackEffect statusUpdate;
	statusUpdate.battleID = battle.getBattle()->getBattleID();
	if(stack->alive() && marker->turnsRemain > 1)
	{
		Bonus nextStatus(*marker);
		nextStatus.turnsRemain = marker->turnsRemain - 1;
		JsonNode nextParameters;
		if(marker->parameters)
		{
			try
			{
				nextParameters = marker->parameters->toCustom<JsonNode>();
			}
			catch(const std::exception &)
			{
			}
		}
		nextParameters["casterSide"].Integer() = static_cast<int32_t>(casterSide);
		nextParameters["spreadAttempts"].Integer() = spreadAttempts < std::numeric_limits<int32_t>::max()
			? spreadAttempts + 1 : spreadAttempts;
		nextParameters["lastProcessedRound"].Integer() = currentRound;
		if(sourceUnitId >= 0)
			nextParameters["sourceUnitId"].Integer() = sourceUnitId;
		nextStatus.parameters = std::make_shared<BonusParameters>(nextParameters);
		// updateUnitBonus only extends turnsRemain and leaves parameters untouched.
		// Remove+add is therefore required to persist the round marker and countdown.
		statusUpdate.toRemove.emplace_back(stack->unitId(), std::vector<Bonus>{*marker});
		statusUpdate.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{std::move(nextStatus)});
	}
	else
		statusUpdate.toRemove.emplace_back(stack->unitId(), std::vector<Bonus>{*marker});
	gameHandler->sendAndApply(statusUpdate);
}
}

bool BattleFlowProcessor::tryStartCrisisCommand(const CBattleInfoCallback & battle,
	const BattleAction * completed, bool masterGate, bool pursuit, bool ranged)
{
	if(battle.battleGetRound() < 1 || battle.battleTacticDist()
		|| battle.getBattle()->getCrisisCommandState().choice()) return false;
	while(!battle.getBattle()->getCrisisCommandState().pending.empty())
	{
		auto next = battle.getBattle()->getCrisisCommandState();
		const auto receipt = next.pending.front();
		next.pending.erase(next.pending.begin());
		const auto * chooserAnchor = newHorizonsCrisisCommand::anchor(battle, receipt.side);
		BattleCrisisCommandChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		if(!chooserAnchor || !newHorizonsCrisisCommand::eligible(battle.battleGetFightingHero(receipt.side)))
		{
			update.state = next;
			gameHandler->sendAndApply(update);
			continue;
		}
		auto ledger = battle.getBattle()->getHeroActionAllowances(receipt.side);
		newHorizonsCrisisCommand::ReturnFrame frame;
		frame.responder = receipt.side;
		frame.anchor = chooserAnchor->unitId();
		frame.round = battle.battleGetRound();
		frame.originalActor = battle.getBattle()->getActiveStackID();
		const auto & seize = battle.getBattle()->getSeizeInitiativeState();
		frame.suspendedSeizeActive = seize.active;
		frame.suspendedSeizeActiveNormal = seize.activeNormal;
		frame.grant = ledger.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::CRISIS_COMMAND, frame.round);
		if(completed)
		{
			frame.kind = newHorizonsCrisisCommand::ReturnKind::ACTION;
			frame.action = *completed;
			frame.masterGate = masterGate; frame.pursuit = pursuit; frame.ranged = ranged;
		}
		next.returns.push_back(frame);
		update.state = next;
		update.allowanceSide = receipt.side;
		update.allowances = ledger;
		gameHandler->sendAndApply(update);
		BattleSetActiveStack activate;
		activate.battleID = update.battleID;
		activate.stack = frame.anchor;
		activate.reason = BattleUnitTurnReason::CRISIS_ORDER;
		gameHandler->sendAndApply(activate);
		const bool anyOrder = std::any_of(heroCommands::CANONICAL_COMMANDS.begin(),
			heroCommands::CANONICAL_COMMANDS.end(), [&battle, &receipt](HeroCommand command)
			{ return battle.battleCanUseHeroCommand(receipt.side, command) || battle.battleCanBeginHeroCommand(receipt.side, command); });
		if(!anyOrder) resumeCrisisCommand(battle);
		return true;
	}
	return false;
}

void BattleFlowProcessor::resumeCrisisCommand(const CBattleInfoCallback & battle)
{
	auto next = battle.getBattle()->getCrisisCommandState();
	if(next.returns.empty()) throw std::runtime_error("No Crisis Command return frame");
	const auto frame = next.returns.back();
	next.returns.pop_back();
	auto ledger = battle.getBattle()->getHeroActionAllowances(frame.responder);
	std::erase_if(ledger.grants, [&frame](const auto & grant)
		{ return grant.id == frame.grant && grant.source == HeroActionAllowanceState::GrantSource::CRISIS_COMMAND; });
	BattleCrisisCommandChanged update;
	update.battleID = battle.getBattle()->getBattleID();
	update.state = next;
	update.allowanceSide = frame.responder;
	update.allowances = ledger;
	gameHandler->sendAndApply(update);
	BattleSetActiveStack restore;
	restore.battleID = update.battleID;
	restore.stack = static_cast<uint32_t>(frame.originalActor);
	restore.reason = BattleUnitTurnReason::CRISIS_RESUME;
	gameHandler->sendAndApply(restore);
	if(frame.kind == newHorizonsCrisisCommand::ReturnKind::ACTION)
		onActionMade(battle, frame.action, frame.masterGate, frame.pursuit, frame.ranged);
	else
		activateNextStack(battle);
}

bool BattleFlowProcessor::declineCrisisCommand(const CBattleInfoCallback & battle,
	PlayerColor player, const BattleAction & action)
{
	const auto & state = battle.getBattle()->getCrisisCommandState();
	if(!state.choice() || action.actionType != EActionType::NO_ACTION
		|| action.side != state.chooser() || player != battle.sideToPlayer(state.chooser())
		|| action.stackNumber != state.returns.back().anchor) return false;
	resumeCrisisCommand(battle);
	return true;
}

void BattleFlowProcessor::onActionMade(const CBattleInfoCallback & battle, const BattleAction &ba,
	bool masterGateActivationContinuation, bool pursuitActivationContinuation, bool rangedAttackContinuation)
{
	const auto * actedStack = battle.battleGetStackByID(ba.stackNumber, false);
	const auto * activeStack = battle.battleActiveUnit();
	const auto completeSeize = [this, &battle, &ba](const battle::Unit * unit)
	{
		const auto & state = battle.getBattle()->getSeizeInitiativeState();
		if(!unit || ba.actionType == EActionType::WAIT || !state.enabled() || state.active != unit->unitId())
			return;
		BattleNormalActivationCompleted update;
		update.battleID = battle.getBattle()->getBattleID();
		update.unitId = unit->unitId();
		update.expected = state;
		gameHandler->sendAndApply(update);
	};
	const auto completeDiscipline = [this, &battle, &ba](const battle::Unit * unit)
	{
		if(!unit)
			return;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto states = battle.getBattle()->getHeroOrderStates(side);
			if(!newHorizonsDivineMandate::completeDisciplineOrders(states, battle.battleGetRound(), unit->unitId(),
				ba.actionType != EActionType::NO_ACTION || !unit->isTimeStopped()))
				continue;
			BattleHeroOrderStateChanged update;
			update.battleID = battle.getBattle()->getBattleID();
			update.side = side;
			update.states = states;
			update.state = states.empty() ? std::optional<HeroOrderState>() : states.back();
			gameHandler->sendAndApply(update);
		}
	};
	const auto startReducedExtraActivation = [this, &battle](const battle::Unit * unit)
	{
		if(!spendQuartermasterAllowance(gameHandler, battle, unit))
			return false;

		setActiveStack(battle, unit, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
		const bool terminalActivation = !unit->alive() || unit->isTimeStopped();
		if(terminalActivation)
			clearQuartermasterActivation(gameHandler, battle, unit->unitId());
		if(owner->checkBattleStateChanges(battle))
			return true;
		if(terminalActivation)
			activateNextStack(battle);
		return true;
	};
	const auto captureRapidResponse = [this, &battle, &ba](const battle::Unit * unit)
	{
		if(!unit || ba.actionType == EActionType::WAIT
			|| (ba.actionType == EActionType::NO_ACTION && unit->isTimeStopped()))
			return;
		const auto actingSide = battle.playerToSide(battle.battleGetOwner(unit));
		if(actingSide == BattleSide::ATTACKER || actingSide == BattleSide::DEFENDER)
			publishRapidResponse(gameHandler, battle, actingSide == BattleSide::ATTACKER
				? BattleSide::DEFENDER : BattleSide::ATTACKER,
				BattleRapidResponseStateChanged::Transition::CAPTURE);
	};
	const auto completeAcceptedActivation = [this, &battle, &ba, &startReducedExtraActivation, &completeDiscipline, &captureRapidResponse, &completeSeize](const battle::Unit * unit)
	{
		if(!unit || ba.actionType == EActionType::WAIT)
			return false;
		captureRapidResponse(unit);
		publishSwiftLifecycle(gameHandler, battle, unit, true);
		completeSeize(unit);
		completeDiscipline(unit);
		if(quartermasterActiveSide(battle, unit->unitId()))
		{
			clearQuartermasterActivation(gameHandler, battle, unit->unitId());
			return false;
		}
		return startReducedExtraActivation(unit);
	};
	if (ba.actionType == EActionType::END_TACTIC_PHASE)
	{
		const auto & deployment = battle.getBattle()->getDeploymentState();
		if(deployment.independent)
		{
			const auto activeSide = deployment.activeSide();
			if(activeSide == BattleSide::NONE || ba.side != activeSide)
			{
				logGlobal->error("Rejected stale or out-of-order deployment completion.");
				return;
			}

			auto nextDeployment = deployment;
			nextDeployment.complete(activeSide);
			BattleDeploymentPhaseChanged update;
			update.battleID = battle.getBattle()->getBattleID();
			update.state = nextDeployment;
			gameHandler->sendAndApply(update);

			// The next entitled side/phase resolves before any battle-start trigger,
			// opening spell, round transition, or activation.
			if(nextDeployment.activeSide() != BattleSide::NONE)
				return;
		}
		onTacticsEnded(battle);
		return;
	}


	//we're after action, all results applied

	// check whether action has ended the battle
	if(owner->checkBattleStateChanges(battle))
		return;
	const auto & crisis = battle.getBattle()->getCrisisCommandState();
	if(crisis.choice())
	{
		if(ba.actionType != EActionType::HERO_COMMAND || ba.side != crisis.chooser())
			throw std::runtime_error("Crisis Command accepted an unrelated action");
		if(ba.command != HeroCommand::SECOND_WIND)
		{
			resumeCrisisCommand(battle);
			return;
		}
		auto next = crisis;
		next.returns.back().phase = newHorizonsCrisisCommand::Phase::GRANTED_EXTRA;
		BattleCrisisCommandChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.state = next;
		gameHandler->sendAndApply(update);
	}
	else if(tryStartCrisisCommand(battle, &ba, masterGateActivationContinuation,
		pursuitActivationContinuation, rangedAttackContinuation))
		return;

	// Redeployment uses the ordinary deployment WALK action. The action processor
	// rejects unchanged destinations before publishing StartAction and only calls
	// this method after an accepted move, so this event consumes exactly one
	// final-relocation opportunity.
	const auto & deploymentAfterAction = battle.getBattle()->getDeploymentState();
	if(deploymentAfterAction.independent && deploymentAfterAction.isFinalRelocation()
		&& ba.actionType == EActionType::WALK)
	{
		const auto activeSide = deploymentAfterAction.activeSide();
		if(activeSide == BattleSide::NONE || ba.side != activeSide)
		{
			logGlobal->error("Accepted final deployment move does not match the active side.");
			return;
		}

		auto nextDeployment = deploymentAfterAction;
		nextDeployment.complete(activeSide);
		BattleDeploymentPhaseChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		update.state = nextDeployment;
		gameHandler->sendAndApply(update);

		if(nextDeployment.activeSide() != BattleSide::NONE)
			return;

		onTacticsEnded(battle);
		return;
	}

	// tactics - next stack will be selected by player
	const auto & deployment = battle.getBattle()->getDeploymentState();
	const auto * battleState = dynamic_cast<const IBattleState *>(battle.getBattle());
	if(deployment.independent ? deployment.activeSide() != BattleSide::NONE
		: (battleState && battleState->getTacticDist() != 0))
		return;

	// Battle Plan Orders are issued through a temporary HERO_COMMAND anchor
	// without beginning a creature activation. After an accepted opening Order,
	// offer the next eligible side before the ordinary turn queue advances.
	if(battle.battleGetRound() == 1 && battle.getBattle()->getActivationSerial() == 0)
	{
		if(tryStartPreCombatOrder(battle))
			return;
		if(ba.actionType == EActionType::HERO_COMMAND)
		{
			activateNextStack(battle);
			return;
		}
	}

	std::optional<uint32_t> deferredDoubleCommandSecondWind;
	if(ba.side == BattleSide::ATTACKER || ba.side == BattleSide::DEFENDER)
	{
		const auto publishContinuation = [this, &battle, &ba](const DoubleCommandState & next)
		{
			BattleHeroOrderStateChanged update;
			update.battleID = battle.getBattle()->getBattleID();
			update.side = ba.side;
			update.states = battle.getBattle()->getHeroOrderStates(ba.side);
			if(!update.states->empty())
				update.state = update.states->back();
			update.doubleCommandState = next;
			gameHandler->sendAndApply(update);
		};
		auto continuation = battle.getBattle()->getDoubleCommandState(ba.side);
		if(continuation.orderPending())
		{
			// This bounded choice check runs only at the accepted action boundary,
			// never during rendering, movement, or ordinary battle updates.
			const bool legalChoice = std::any_of(
				heroCommands::CANONICAL_COMMANDS.begin(), heroCommands::CANONICAL_COMMANDS.end(),
				[&battle, &ba](const auto command)
				{
					return battle.battleCanUseHeroCommand(ba.side, command)
						|| battle.battleCanBeginHeroCommand(ba.side, command);
				});
			if(legalChoice)
			{
				const auto * anchor = battle.battleGetStackByID(continuation.anchorStackId, false);
				if(!anchor || !anchor->alive())
					throw std::runtime_error("Double Command lost its active choice anchor");
				setActiveStack(battle, anchor, BattleUnitTurnReason::HERO_COMMAND);
				return;
			}
			continuation.exhaustPendingOrder();
			publishContinuation(continuation);
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Double Command ends: no different legal Order remains.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		if(continuation.secondWindReady())
		{
			deferredDoubleCommandSecondWind = continuation.consumeSecondWindContinuation();
			publishContinuation(continuation);
		}
	}

	// Resolve the deferred activation before unrelated creature continuations.
	// Otherwise a ranged follow-up could return after consuming the saved target.
	if((ba.actionType == EActionType::HERO_COMMAND && ba.command == HeroCommand::SECOND_WIND)
		|| deferredDoubleCommandSecondWind)
	{
		const auto state = battle.getBattle()->getHeroOrderState(ba.side, HeroCommand::SECOND_WIND);
		const auto targetId = deferredDoubleCommandSecondWind.value_or(
			state ? state->primaryTargetUnitId : HeroOrderState::INVALID_UNIT_ID);
		const auto * target = state && targetId == state->primaryTargetUnitId
			&& targetId != HeroOrderState::INVALID_UNIT_ID
			? battle.battleGetStackByID(targetId, false) : nullptr;
		if(const auto * stateInfo = dynamic_cast<const BattleInfo *>(battle.getBattle());
			target && target->alive() && stateInfo
			&& !newHorizonsSwiftRebirth::blocksAdditionalActivation(*target, battle.battleGetRound())
			&& const_cast<BattleInfo *>(stateInfo)->setHeroOrderSecondWindActive(ba.side, true))
		{
			publishHeroOrderState(battle, ba.side);
			setActiveStack(battle, target, BattleUnitTurnReason::HERO_COMMAND);
			// A lethal start-of-activation Fire Wall must not leave a dead anchor.
			if(!target->alive())
				activateNextStack(battle);
			return;
		}
	}

	if(rangedAttackContinuation)
	{
		if(activeStack && activeStack->alive() && battle.battleCanTakeRangedFollowUp(activeStack))
			setActiveStack(battle, activeStack, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
		else
		{
			if(battle.battleHasPendingRangedFollowUp(activeStack))
			{
				auto state = activeStack->acquireState();
				state->setRangedFollowUpDamagePercent(0);
		state->luckyOwnAttackSequence = false;
				state->luckyOwnAttackSequence = false;
				BattleUnitsChanged update;
				update.battleID = battle.getBattle()->getBattleID();
				UnitChanges change(activeStack->unitId(), UnitChanges::EOperation::UPDATE);
				change.data = state->save();
				update.changedStacks.push_back(std::move(change));
				gameHandler->sendAndApply(update);
			}
			if(!completeAcceptedActivation(actedStack ? actedStack : activeStack))
				activateNextStack(battle);
		}
		return;
	}

	// A Hero Action can make an already-earned shot unusable (for example by
	// applying stasis). Do not leave a stale allowance that blocks every creature
	// action; discard it and end this activation once.
	const auto * followUpStack = actedStack ? actedStack : activeStack;
	if(battle.battleHasPendingRangedFollowUp(followUpStack))
	{
		auto state = followUpStack->acquireState();
		state->setRangedFollowUpDamagePercent(0);
		BattleUnitsChanged update;
		update.battleID = battle.getBattle()->getBattleID();
		UnitChanges change(followUpStack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString("The Master Gunner follow-up is no longer usable.");
		message.lines.push_back(std::move(line));
		gameHandler->sendAndApply(message);
		if(!completeAcceptedActivation(followUpStack))
			activateNextStack(battle);
		return;
	}

	if(masterGateActivationContinuation)
	{
		if(actedStack && activeStack == actedStack && actedStack->alive() && !actedStack->isTimeStopped())
			setActiveStack(battle, actedStack, BattleUnitTurnReason::MASTER_GATE_CONTINUATION);
		else
		{
			if(!(ba.isUnitAction() && completeAcceptedActivation(actedStack)))
				activateNextStack(battle);
		}
		return;
	}

	if(pursuitActivationContinuation)
	{
		if(actedStack && activeStack == actedStack && actedStack->alive()
			&& !actedStack->isTimeStopped() && actedStack->pursuitMovementRemaining > 0)
			setActiveStack(battle, actedStack, BattleUnitTurnReason::PURSUIT_CONTINUATION);
		else
		{
			if(!(ba.isUnitAction() && completeAcceptedActivation(actedStack)))
				activateNextStack(battle);
		}
		return;
	}

	if(ba.timeStopHeroActionPass)
	{
		// This pass consumes only the due Hero Action boundary. If removing that
		// origin released the anchor, return creature control as a continuation;
		// otherwise drain/schedule the remaining Time Stop origins normally.
		if(activeStack && activeStack->alive() && !activeStack->isTimeStopped())
			setActiveStack(battle, activeStack, BattleUnitTurnReason::HERO_COMMAND);
		else
		{
			if(activeStack)
				clearQuartermasterActivation(gameHandler, battle, activeStack->unitId());
			activateNextStack(battle);
		}
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

	if (ba.isUnitAction())
	{
		assert(activeStack != nullptr);
		assert(actedStack != nullptr);

		// Continuations and free creature casts returned above. A completed
		// accepted action still spends Swift's normal slot if its retaliation
		// or end effects incapacitated the actor before reaching this boundary.
		if(ba.actionType != EActionType::WAIT)
		{
			captureRapidResponse(actedStack);
			publishSwiftLifecycle(gameHandler, battle, actedStack, true);
			completeSeize(actedStack);
			completeDiscipline(actedStack);
		}

		if(actedStack->isTimeStopped())
		{
			// The automatic no-op only advances the queue.  Do not grant morale,
			// Orders, or any other second activation to a stopped stack.
			clearQuartermasterActivation(gameHandler, battle, actedStack->unitId());
			activateNextStack(battle);
			return;
		}
		if(newHorizonsFrozen::isFrozen(*actedStack))
		{
			// A retaliation may freeze the acting stack during its own attack.
			// That already-spent action is not its next normal forfeited slot.
			// Only the automatic queue no-op thaws; neither path grants extras.
			if(ba.actionType != EActionType::WAIT)
			{
				// Frozen forfeits an ordinary activation; it does not pause the
				// existing end-of-activation affliction lifecycle like Time Stop.
				applyPlagueEndOfActivation(gameHandler, battle, actedStack);
				if(owner->checkBattleStateChanges(battle))
					return;
			}
			if(ba.actionType == EActionType::NO_ACTION)
			{
				SetStackEffect thaw;
				thaw.battleID = battle.getBattle()->getBattleID();
				thaw.toRemove.emplace_back(actedStack->unitId(), newHorizonsFrozen::removalPlan(*actedStack));
				gameHandler->sendAndApply(thaw);
			}
			clearQuartermasterActivation(gameHandler, battle, actedStack->unitId());
			const auto side = battle.playerToSide(battle.battleGetOwner(actedStack));
			if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			{
				const auto secondWind = battle.getBattle()->getHeroOrderState(side, HeroCommand::SECOND_WIND);
				if(secondWind && secondWind->secondWindActive
					&& secondWind->primaryTargetUnitId == actedStack->unitId())
				{
					if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
					{
						if(const_cast<BattleInfo *>(state)->setHeroOrderSecondWindActive(side, false))
							publishHeroOrderState(battle, side);
					}
				}
			}
			activateNextStack(battle);
			return;
		}

		const auto controllerSide = battle.playerToSide(battle.battleGetOwner(actedStack));
		if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		{
			if(const auto state = battle.getBattle()->getHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
				state && state->secondWindActive
				&& state->primaryTargetUnitId == actedStack->unitId())
			{
				if(const auto * stateInfo = dynamic_cast<const BattleInfo *>(battle.getBattle()))
				{
					if(const_cast<BattleInfo *>(stateInfo)->setHeroOrderSecondWindActive(controllerSide, false))
						publishHeroOrderState(battle, controllerSide);
				}
			}
		}

		if(ba.actionType != EActionType::WAIT)
		{
			applyPlagueEndOfActivation(gameHandler, battle, actedStack);
			if(owner->checkBattleStateChanges(battle))
				return;
			if(!actedStack->alive())
			{
				clearQuartermasterActivation(gameHandler, battle, actedStack->unitId());
				activateNextStack(battle);
				return;
			}
		}

		const bool reducedExtraActivation = quartermasterActiveSide(battle, actedStack->unitId()).has_value();
		if (!reducedExtraActivation && rollGoodMorale(battle, actedStack))
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
		if(ba.actionType != EActionType::WAIT && completeAcceptedActivation(actedStack))
			return;
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
		if(activeStack && (!activeStack->alive() || activeStack->isTimeStopped() || !activeStack->canMove()))
			clearQuartermasterActivation(gameHandler, battle, activeStack->unitId());
	}

	activateNextStack(battle);
}

bool BattleFlowProcessor::makeStackDoNothing(const CBattleInfoCallback & battle, const CStack * next)
{
	return makeAutomaticAction(battle, next, BattleAction::makeNoAction(next));
}

bool BattleFlowProcessor::makeAutomaticAction(const CBattleInfoCallback & battle, const CStack *stack, const BattleAction &ba)
{
	if(!beginAutomaticActivation(battle, stack))
		return true;
	return owner->makeAutomaticBattleAction(battle, ba);
}

bool BattleFlowProcessor::beginAutomaticActivation(const CBattleInfoCallback & battle, const CStack * stack)
{
	if(!newHorizonsSwiftRebirth::normalActivationCompleted(*stack, battle.battleGetRound()))
		publishSwiftLifecycle(gameHandler, battle, stack, false);
	BattleSetActiveStack bsa;
	bsa.battleID = battle.getBattle()->getBattleID();
	bsa.stack = stack->unitId();
	bsa.reason = BattleUnitTurnReason::AUTOMATIC_ACTION;
	gameHandler->sendAndApply(bsa);
	if(battle.battleBeginsActivation(stack, bsa.reason))
		applyStartOfActivationEffects(gameHandler, battle, stack);
	if(!stack->alive())
		return false;
	// Automatic actions still represent a fresh creature activation. Trigger
	// passable Fire Wall footprints after the authoritative nextTurn packet so
	// their activation serial is current and movement callbacks cannot repeat
	// the same damage.
	if(!stack->isTimeStopped() && canonicalFireWallCoversUnit(battle, *stack))
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);
	if(!stack->alive())
		return false;
	return true;
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
				manaDrained = static_cast<ui32>(std::min<int64_t>(manaDrained, opponentHero->getManaAvailable()));
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
			const int chance = battle.battleGetFearChance(st);
			const auto * controllerHero = battle.battleGetOwnerHero(st);
			const bool fearlessCanceledFear = newHorizonsDiscipline::hasFearless(controllerHero)
				&& chance <= 0 && st->valOfBonuses(BonusType::FEARFUL) > 0;
			if(fearlessCanceledFear)
			{
				BattleLogMessage feedback;
				feedback.battleID = battle.getBattle()->getBattleID();
				MetaString line;
				if(controllerHero)
				{
					line.appendTextID(controllerHero->getNameTextID());
					line.appendRawString(": ");
				}
				line.appendRawString("Discipline: Fearless protects ");
				line.appendName(st->creatureId(), st->getCount());
				line.appendRawString(" from a non-magical fear check.");
				feedback.lines.push_back(std::move(line));
				gameHandler->sendAndApply(feedback);
			}
			else
			{
				ObjectInstanceID opponentArmyID = battle.battleGetArmyObject(battle.otherSide(st->unitSide()))->id;
				const auto affectedSide = battle.playerToSide(battle.battleGetOwner(st));
				const auto drawFearful = [this, opponentArmyID, chance]()
				{
					return gameHandler->randomizer->rollCombatAbility(opponentArmyID, chance);
				};
				const bool fearful = owner->resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), affectedSide,
					chance > 0 && chance < 100, true, drawFearful);

				if(fearful)
				{
					bte.effect = BonusType::FEARFUL;
					gameHandler->sendAndApply(bte);
				}
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
	bool swiftExtra = reason == BattleUnitTurnReason::MORALE
		|| reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto side = battle.playerToSide(battle.battleGetOwner(stack));
		if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		{
			const auto secondWind = battle.getBattle()->getHeroOrderState(side, HeroCommand::SECOND_WIND);
			swiftExtra = secondWind && secondWind->secondWindActive
				&& secondWind->primaryTargetUnitId == stack->unitId();
		}
	}
	if(swiftExtra && newHorizonsSwiftRebirth::blocksAdditionalActivation(*stack, battle.battleGetRound()))
	{
		clearQuartermasterActivation(gameHandler, battle, stack->unitId());
		if(reason == BattleUnitTurnReason::HERO_COMMAND)
		{
			const auto side = battle.playerToSide(battle.battleGetOwner(stack));
			if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()); state
				&& (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
				&& const_cast<BattleInfo *>(state)->setHeroOrderSecondWindActive(side, false))
				publishHeroOrderState(battle, side);
		}
		activateNextStack(battle);
		return;
	}
	if(reason == BattleUnitTurnReason::TURN_QUEUE)
		publishSwiftLifecycle(gameHandler, battle, stack, false);
	if(newHorizonsFrozen::isFrozen(*stack) && reason != BattleUnitTurnReason::TURN_QUEUE
		&& reason != BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		// Earned extras/continuations are unusable, not normal queue forfeitures.
		// Do not remove Frozen or run any start-of-activation effects here.
		clearQuartermasterActivation(gameHandler, battle, stack->unitId());
		const auto side = battle.playerToSide(battle.battleGetOwner(stack));
		if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		{
			const auto secondWind = battle.getBattle()->getHeroOrderState(side, HeroCommand::SECOND_WIND);
			if(secondWind && secondWind->secondWindActive
				&& secondWind->primaryTargetUnitId == stack->unitId())
			{
				if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
				{
					if(const_cast<BattleInfo *>(state)->setHeroOrderSecondWindActive(side, false))
						publishHeroOrderState(battle, side);
				}
			}
		}
		activateNextStack(battle);
		return;
	}

	BattleSetActiveStack sas;
	sas.battleID = battle.getBattle()->getBattleID();
	sas.stack = stack->unitId();
	sas.reason = reason;
	gameHandler->sendAndApply(sas);
	if(battle.battleBeginsActivation(stack, reason))
		applyStartOfActivationEffects(gameHandler, battle, stack);
	bool secondWindActivation = false;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto controllerSide = battle.playerToSide(battle.battleGetOwner(stack));
		if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		{
			const auto state = battle.getBattle()->getHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
			secondWindActivation = state
				&& state->secondWindActive
				&& state->primaryTargetUnitId == stack->unitId();
		}
	}
	if(!stack->isTimeStopped()
		&& (reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::MORALE
			|| reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION || secondWindActivation)
		&& canonicalFireWallCoversUnit(battle, *stack))
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);
}

namespace
{
void applyStartOfActivationEffects(CGameHandler * gameHandler,
	const CBattleInfoCallback & battle, const battle::Unit * stack)
{
	const auto * creatureStack = dynamic_cast<const CStack *>(stack);
	if(!creatureStack)
		return;

	const auto * hero = battle.battleGetOwnerHero(creatureStack);
	auto state = creatureStack->acquireState();
	if(state)
	{
		const int64_t pendingPhysicalDamage = state->veteranPhysicalDamageSinceActivation;
		const int64_t healing = newHorizonsCombatSkills::applyVeteran(state.get(), hero);
		if(pendingPhysicalDamage > 0 || healing > 0)
		{
			UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			update.healthDelta = healing;
			BattleUnitsChanged changed;
			changed.battleID = battle.getBattle()->getBattleID();
			changed.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(changed);

			if(healing > 0)
			{
				BattleLogMessage message;
				message.battleID = battle.getBattle()->getBattleID();
				MetaString line;
				line.appendRawString("Veteran restores %s ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(healing);
				line.appendRawString(" Health.");
				message.lines.push_back(std::move(line));
				gameHandler->sendAndApply(message);
			}
		}
	}
	if(creatureStack->alive() && !creatureStack->isTimeStopped() && state
		&& state->regenerationPendingMicroHealth > 0)
	{
		int64_t healing = state->consumeRegenerationMarks();
		if(healing > 0)
			healing = state->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Regeneration restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
	static const SpellID hydrasVitalitySpell(SpellID::decode("new-horizons:hydrasVitality"));
	const auto hydrasVitalityMarker = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::HP_REGENERATION));
	if(creatureStack->alive() && !creatureStack->isTimeStopped() && state
		&& state->health.isCapacityHealthTracking()
		&& creatureStack->hasBonus(hydrasVitalityMarker))
	{
		const int64_t healing = state->consumeCapacityRegeneration();
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Hydra's Vitality restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
	const int64_t poisonTick = newHorizonsBulwark::physicalPoisonTickDamage(state.get());
	if(creatureStack->alive() && state && poisonTick > 0)
	{
		const int64_t guardianSpiritBefore = state->guardianSpiritHitPoints;
		const auto poisonSourceStackId = state->physicalPoisonSourceStackId;
		newHorizonsBulwark::advancePhysicalPoison(state.get());
		BattleStackAttacked hit;
		hit.attackerID = poisonSourceStackId >= 0
			? static_cast<ui32>(poisonSourceStackId) : creatureStack->unitId();
		hit.stackAttacked = creatureStack->unitId();
		hit.damageAmount = poisonTick;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state,
			false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		const int64_t guardianSpiritAbsorbed = std::max<int64_t>(
			0, guardianSpiritBefore - state->guardianSpiritHitPoints);
		const int64_t guardianSpiritOverflow = guardianSpiritAbsorbed > 0
			? std::max<int64_t>(0, poisonTick - guardianSpiritAbsorbed) : 0;
		if(!state->alive())
		{
			newHorizonsBulwark::clearPhysicalPoison(state.get());
			hit.newState.data = state->save();
		}
		StacksInjured injury;
		injury.battleID = battle.getBattle()->getBattleID();
		injury.stacks.push_back(hit);
		gameHandler->sendAndApply(injury);

		if(hit.damageAmount > 0 || guardianSpiritAbsorbed > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			if(guardianSpiritAbsorbed > 0)
			{
				line.appendRawString("Physical Poison hits %s; Guardian Spirit absorbs ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(guardianSpiritAbsorbed);
				line.appendRawString(" physical damage");
				if(guardianSpiritOverflow > 0)
				{
					line.appendRawString(" and ");
					line.appendNumber(guardianSpiritOverflow);
					line.appendRawString(" damage passes through.");
				}
				else
					line.appendRawString("; no damage passes through.");
			}
			else
			{
				line.appendRawString("%s suffers ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(hit.damageAmount);
				line.appendRawString(" physical Poison damage.");
			}
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		state = creatureStack->acquireState();
	}
	if(state && state->bulwarkMireGripApplied)
	{
		const int skillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		const auto sourceId = BonusSourceID(SecondarySkill(skillId));
		const auto penalties = creatureStack->getAllBonuses(Selector::source(BonusSource::OTHER, sourceId));
		if(penalties && !penalties->empty())
		{
			std::vector<Bonus> toRemove;
			toRemove.reserve(penalties->size());
			for(const auto & bonus : *penalties)
				toRemove.push_back(*bonus);
			SetStackEffect remove;
			remove.battleID = battle.getBattle()->getBattleID();
			remove.toRemove.emplace_back(creatureStack->unitId(), std::move(toRemove));
			gameHandler->sendAndApply(remove);
		}
		state->bulwarkMireGripApplied = false;
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}
	if(creatureStack->alive() && state && state->bulwarkDefendPhysicalDamage > 0
		&& newHorizonsBulwark::hasSwampRenewal(hero))
	{
		const int64_t healing = newHorizonsBulwark::applySwampRenewal(state.get(), hero);

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Swamp Renewal restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
}
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
