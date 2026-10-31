/*
 * HypotheticActivation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PrismaticVanguardProjection.h"
#include "PhysicalStrikeProjection.h"
#include "DreadCavalryProjection.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "InitialExportBattleSubject.h"
#include "../../lib/battle/RoundBonusDuration.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "HypotheticLifecycleTimingScope.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

using hypotheticLifecycleDiagnostics::LifecycleTimingScope;

void HypotheticBattle::nextRound()
{
	nextRound(nullptr);
}

void HypotheticBattle::nextRound(HypotheticLifecycleTiming * timing)
{
	if(deploymentState.independent && deploymentState.activeSide() != BattleSide::NONE)
		throw std::runtime_error("Cannot advance a battle round with unresolved deployment phase");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(preCombatOrderStates.at(side).isUnresolved())
			throw std::runtime_error("Cannot advance a battle round with unresolved Battle Plan choice");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(doubleCommandStates.at(side).orderPending()
			|| doubleCommandStates.at(side).secondWindReady())
			throw std::runtime_error("Cannot advance a battle round with unresolved Double Command continuation");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		fortuneStates[side].nextRound();
		luckSerendipityStates[side].nextRound(projectedRound + 1);
		moraleSuppressionStates[side].nextRound();
		rapidResponseStates[side].clearPending();
		std::erase_if(heroOrderStates[side], [this](const HeroOrderState & order)
			{ return !order.hasScheduledRecipients(projectedRound + 1); });
	}
	for(auto & [side, state] : focusFireStates)
		if(!getHeroOrderState(side, HeroCommand::FOCUS_FIRE))
			state.reset();
	++bonusTreeVersion;
	// BattleInfo grants opening effects their full duration in round one.
	const bool firstRound = projectedRound == 0;
	++projectedRound;
	seizeInitiative.nextRound(projectedRound);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & spellResponse = spellResponseStates.at(side);
		if(spellResponse.hasState() && !spellResponse.isReadyAt(projectedRound))
			spellResponse = {};
		warcastingStates[side] = warcastingStates[side].clearedIfExpired(projectedRound);
		if(heroActionAllowances[side].currentRound >= 0)
			heroActionAllowances[side].resetForRound(projectedRound);
		auto & meta = metamagicStates[side];
		meta.pending = 0;
		meta.firstSpell = SpellID();
		meta.firstTarget = std::numeric_limits<uint32_t>::max();
		meta.firstCountered = false;
		meta.sequence.clear();
	}
	std::vector<uint32_t> pendingRemoval;
	std::vector<uint32_t> pendingFormRestoration;
	const auto owned = initialExportSubject();
	const auto ownedRoundSelector = owned ? battle::roundTimedBonuses(getMagicRules()) : CSelector();
	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto forUpdate = getForUpdate(unit->unitId());
		// Capture pauses before timed bonuses age, matching the live round hook.
		const bool formPaused = battle::battleFormDurationPaused(*forUpdate);
		if(!firstRound && !formPaused && forUpdate->hasBattleForm()
			&& forUpdate->getBattleFormRoundsRemaining() <= 1)
			pendingFormRestoration.push_back(unit->unitId());
		forUpdate->clearNoQuarterRoundBlocker();
		if(!firstRound && !forUpdate->isTimeStopped())
		{
			LifecycleTimingScope measurement(timing ? &timing->roundTimedUpdates : nullptr);
			if(owned)
				owned->advanceTimedRound(*this, unit->unitId(), ownedRoundSelector);
			else
				forUpdate->advanceTimedRound(timing ? &timing->roundTimedWork : nullptr);
		}
		forUpdate->afterNewRound(firstRound, true, formPaused);
		if(forUpdate->ghostPending)
			pendingRemoval.push_back(unit->unitId());
	}
	// The authoritative flow flushes pending ghosts after round updates. This
	// also releases originals whose clones lost their duration marker.
	for(const auto id : pendingRemoval)
		removeUnit(id);
	std::ranges::sort(pendingFormRestoration);
	for(const auto id : pendingFormRestoration)
	{
		const auto unit = getForUpdate(id);
		if(!unit->hasBattleForm() || unit->isGhost())
			continue;
		// Each successful return can occupy a new footprint. Recompute from the
		// updated branch before restoring the next canonical-ID recipient.
		if(battle::endBattleFormAtNearestLegalPosition(*unit, getAccessibility(unit.get())))
			unit->removeUnitBonus(CSelector(battle::isPolymorphMarker));
	}
	// Match the authoritative post-BattleNextRound health phase: timed expiry,
	// pending ghosts and form restoration are already resolved; no activation occurs.
	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto healing = static_cast<int64_t>(newHorizonsHeroes::ordinaryCreatureRoundHealing(*this, *unit));
		if(healing > 0)
			getForUpdate(unit->unitId())->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
	// Unlike opening unit enchantments, obstacle timers tick on every round
	// transition. Authoritative BattleFlowProcessor then removes zero timers.
	for(auto & obstacle : projectedObstacles)
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
		{
			auto aged = std::make_shared<SpellCreatedObstacle>(*spell);
			aged->battleTurnPassed();
			obstacle = aged;
		}
	}
	const auto previousSize = projectedObstacles.size();
	vstd::erase_if(projectedObstacles, [](const auto & obstacle)
	{
		const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		return spell && spell->turnsRemaining == 0;
	});
	obstacleChanges |= previousSize != projectedObstacles.size();
	if(owned)
		owned->captureBranchValues(*this);
}

bool HypotheticBattle::projectEarnedMoraleActivation(uint32_t unitId)
{
	auto unit = getForUpdate(unitId);
	if(!unit || !unit->alive() || !unit->canMove() || unit->hadMorale || unit->defending
		|| unit->waited() || unit->fear || battleGetMorale(unit.get()) <= 0
		|| newHorizonsSwiftRebirth::blocksAdditionalActivation(*unit, getRound()))
		return false;
	nextTurn(unitId, BattleUnitTurnReason::MORALE);
	return true;
}

void HypotheticBattle::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
	nextTurn(unitId, reason, nullptr);
}

void HypotheticBattle::nextTurn(uint32_t unitId, BattleUnitTurnReason reason, HypotheticLifecycleTiming * timing)
{
	nextTurnImpl(unitId, reason, false, timing);
}

void HypotheticBattle::nextTurnPreservingFrozenForfeit(uint32_t unitId, BattleUnitTurnReason reason,
	HypotheticLifecycleTiming * timing)
{
	const auto * unit = battleGetUnitByID(unitId);
	if(!unit || !newHorizonsFrozen::forfeitsNormalActivation(*unit, reason))
		throw std::invalid_argument("Deferred Frozen thaw requires a Frozen normal forfeiture");
	nextTurnImpl(unitId, reason, true, timing);
}

void HypotheticBattle::applyProjectedActivationStartEffects(uint32_t unitId,
	const std::shared_ptr<StackWithBonuses> & unit, BattleUnitTurnReason reason,
	HypotheticLifecycleTiming * timing)
{
	// Owned expiry journals serialize before afterGetsTurn. MORALE is already
	// the accepted activation reason, so retain its earned receipt with the marker.
	if(reason == BattleUnitTurnReason::MORALE)
		unit->hadMorale = true;
	unit->moraleExtraActivation = getMoraleExtraDamagePercent() != 100 && reason == BattleUnitTurnReason::MORALE;
	const bool redeploy = unit->defended() && !unit->armorerLastStandDefending;
	const bool waited = unit->waited();
	newHorizonsHeroicSpirit::beginActivation(*unit, reason);
	prismaticVanguardAI::begin(*this, *unit, reason);
	{
		LifecycleTimingScope measurement(timing ? &timing->activationExpiryRemoval : nullptr);
		unit->removeUnitBonus(CSelector(Bonus::UntilNextCreatureActivation),
			timing ? &timing->activationExpiryWork : nullptr);
	}

	// STACK_GETS_TURN is deliberately not globally broadened for HERO_COMMAND
	// transitions. Gambler's own penalty ends on every genuine activation,
	// including Second Wind, without expiring unrelated bonuses early.
	{
		LifecycleTimingScope measurement(timing ? &timing->activationGamblerRemoval : nullptr);
		unit->removeGamblerLuckPenalty(timing ? &timing->activationGamblerWork : nullptr);
	}

	const auto activationSide = playerToSide(battleGetOwner(unit.get()));
	const auto * veteranHero = activationSide == BattleSide::ATTACKER || activationSide == BattleSide::DEFENDER
		? battleGetFightingHero(activationSide) : nullptr;
	newHorizonsCombatSkills::applyVeteran(unit.get(), veteranHero);

	// Match the authoritative BattleFlowProcessor activation-start order:
	// Regeneration consumes only marks already recorded for this survivor,
	// before poison or any other start-of-activation damage can occur.
	if(unit->alive() && !unit->isTimeStopped() && unit->regenerationPendingMicroHealth > 0)
	{
		auto healing = unit->consumeRegenerationMarks();
		if(healing > 0)
			unit->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
	const auto hydrasVitalityMarker = newHorizonsMagic::hydrasVitalitySource(getMagicRules())
		.And(Selector::type()(BonusType::HP_REGENERATION));
	if(unit->alive() && !unit->isTimeStopped()
		&& unit->getCapacityHealthReferenceMax() > 0
		&& unit->health.isCapacityHealthTracking()
		&& unit->hasBonus(hydrasVitalityMarker))
		unit->consumeCapacityRegeneration();

	int64_t maintenanceHealing = newHorizonsMaintenanceSpecialties::activationHealing(*this, *unit, reason);
	if(maintenanceHealing > 0)
		unit->heal(maintenanceHealing, EHealLevel::HEAL, EHealPower::PERMANENT);

	auto poisonDamage = newHorizonsBulwark::physicalPoisonTickDamage(unit.get());
	if(poisonDamage > 0)
	{
		const bool wasAlive = unit->alive();
		const auto rebirthSource = wasAlive ? captureElementalRebirthSource(*unit) : std::nullopt;
		const bool nativeRebirth = hasReadyNativeRebirth(unit.get());
		const auto destroyed = PhysicalStrikeProjection::captureForecastDamage(*this, *unit);
		unit->damage(poisonDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		if(wasAlive && !unit->alive())
			releaseSerpentBindingsForDeath(*unit);
		if(destroyed) PhysicalStrikeProjection::completeDamagePacket(*this, std::span(&*destroyed, 1));
		if(rebirthSource)
			projectElementalRebirth(unit.get(), *rebirthSource,
				wasAlive && !unit->alive(), unit->isClone(), nativeRebirth);
		newHorizonsBulwark::advancePhysicalPoison(unit.get());
	}
	if(unit->bulwarkMireGripApplied)
	{
		const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		if(bulwarkSkillId >= 0)
			unit->removeUnitBonus(Selector::source(BonusSource::OTHER,
				BonusSourceID(SecondarySkill(bulwarkSkillId))));
		unit->bulwarkMireGripApplied = false;
	}
	if(unit->alive())
		newHorizonsBulwark::applySwampRenewal(unit.get(), battleGetOwnerHero(unit.get()));
	const auto side = playerToSide(battleGetOwner(unit.get()));
	for(auto owner : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		fortuneStates.at(owner).beginActivation(unitId, side == owner, redeploy, waited);
}

void HypotheticBattle::nextTurnImpl(uint32_t unitId, BattleUnitTurnReason reason, bool deferFrozenThaw,
	HypotheticLifecycleTiming * timing)
{
	if(reason == BattleUnitTurnReason::CRISIS_ORDER || reason == BattleUnitTurnReason::CRISIS_RESUME)
	{
		dreadCavalryAI::retireForActorHandoff(*this, unitId, reason);
		activeUnitId = static_cast<int32_t>(unitId);
		return;
	}
	auto unit = getForUpdate(unitId);
	bool extra = reason == BattleUnitTurnReason::MORALE
		|| reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto side = playerToSide(battleGetOwner(unit.get()));
		if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		{
			const auto secondWind = getHeroOrderState(side, HeroCommand::SECOND_WIND);
			extra = secondWind && secondWind->secondWindActive && secondWind->primaryTargetUnitId == unitId;
		}
	}
	if(extra && newHorizonsSwiftRebirth::blocksAdditionalActivation(*unit, getRound()))
		return;
	if(reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		seizeInitiative.begin(unitId, true);
		if(newHorizonsSwiftRebirth::normalActivationCompleted(*unit, getRound()))
			return;
		if(const auto next = newHorizonsSwiftRebirth::reserveNormalActivationPlan(*unit, getRound()))
			updateUnitBonus(unitId, {*next});
	}
	if(reason == BattleUnitTurnReason::TURN_QUEUE)
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			if(newHorizonsRapidResponse::pendingWaiter(*this, side) == unit.get())
				setRapidResponseState(side, newHorizonsRapidResponse::resolve(*this, side, true));
			else if(getRapidResponseState(side).pending() && !newHorizonsRapidResponse::pendingWaiter(*this, side))
				setRapidResponseState(side, newHorizonsRapidResponse::resolve(*this, side, false));
		}
	dreadCavalryAI::retireForActorHandoff(*this, unitId, reason);
	activeUnitId = unitId;
	if(extra)
		seizeInitiative.begin(unitId, false);
	const bool frozenNormalSlot = newHorizonsFrozen::forfeitsNormalActivation(*unit, reason);
	if(frozenNormalSlot || (unit->isTimeStopped()
		&& (reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::AUTOMATIC_ACTION)))
	{
		const auto next = newHorizonsSwiftRebirth::completeNormalActivationPlan(*unit, getRound());
		if(next)
			updateUnitBonus(unitId, {*next});
		newHorizonsDivineMandate::completeDisciplineActivation(*this, unitId, frozenNormalSlot);
	}
	// An extra opportunity cannot spend the normal-slot incapacitation receipt.
	if(newHorizonsFrozen::isFrozen(*unit) && !frozenNormalSlot)
		return;
	if(reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
		|| reason == BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION)
		return;
	if(battleBeginsActivation(unit.get(), reason))
	{
		releaseSerpentBindings(unitId);
		applyProjectedActivationStartEffects(unitId, unit, reason, timing);
	}
	bool newActivation = reason != BattleUnitTurnReason::HERO_SPELLCAST
		&& reason != BattleUnitTurnReason::UNIT_SPELLCAST;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		newActivation = false;
		if(unit)
		{
			const auto controllerSide = playerToSide(battleGetOwner(unit.get()));
			if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
			{
				const auto orderState = getHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
				newActivation = orderState
					&& orderState->secondWindActive
					&& orderState->primaryTargetUnitId == unitId;
			}
		}
	}
	if(newActivation)
	{
		unit->forestAmbush.beginActivation(reason == BattleUnitTurnReason::TURN_QUEUE && unit->waiting);
		if(reason == BattleUnitTurnReason::HERO_COMMAND)
			unit->castSpellThisTurn = false; // Match the actual armed Second Wind activation, not ordinary Orders.
		// Match the authoritative activation boundary: a rescued unit stops only
		// this activation, and its temporary Last Stand state expires when it
		// genuinely receives another one.
		unit->armorerLastStandEndedActivation = false;
		unit->armorerLastStandDefending = false;
		unit->pursuitMovementRemaining = 0;
		unit->cleaveUsedThisActivation = false;
		unit->dreadCavalryState = {};
		unit->luckyOwnAttackSequence = false;
		const auto side = playerToSide(battleGetOwner(unit.get()));
		const bool ordinaryCreature = unit->alive() && !unit->isGhost() && !unit->isTurret()
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
		const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
			? battleGetFightingHero(side) : nullptr;
		if(ordinaryCreature && hero
			&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
			relentlessAssaultStates.at(side).beginActivation();
	}

	if(!unit->isTimeStopped() && reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
		unit->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilGetsTurn(bonus)
				&& !newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));

	if(battleBeginsActivation(unit.get(), reason))
		unit->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
			battleGetOwnerHero(unit.get()), unit.get(), reason));

	unit->afterGetsTurn(reason);
	if(frozenNormalSlot)
	{
		unit->movedThisRound = true;
		if(!deferFrozenThaw)
			unit->removeUnitBonus(newHorizonsFrozen::removalPlan(*unit));
	}
	if(const auto owned = initialExportSubject())
		owned->captureBranchValues(*this);
}

void HypotheticBattle::makeWait(const battle::Unit * activeStack)
{
	auto unit = getForUpdate(activeStack->unitId());
	if(const auto next = newHorizonsSwiftRebirth::reserveNormalActivationPlan(*unit, getRound()))
		updateUnitBonus(unit->unitId(), {*next});

	resetActiveUnit();
	newHorizonsForestAmbush::acceptWait(*this, *unit);
	unit->afterWait();
	prismaticVanguardAI::completeWait(*unit);
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		fortuneStates.at(side).tactics.endActivation();
	if(newHorizonsBattlecraft::hasOverwatch(battleGetOwnerHero(unit.get()))
		&& unit->isShooter() && unit->battlecraftOverwatchUsedRound != getRound())
		unit->battlecraftOverwatchReadyRound = getRound();
	const auto side = playerToSide(battleGetOwner(unit.get()));
	const auto round = getRound();
	if(newHorizonsBattlecraft::canAwardBattlefieldMastery(battleGetOwnerHero(unit.get()), unit.get(),
		round, getBattlecraftMasteryAwardRound(side), BattlecraftMasteryAction::WAIT))
		awardBattlecraftMastery(side, unit->unitId(), round, BattlecraftMasteryAction::WAIT);
	unit->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
		battleGetOwnerHero(unit.get()), unit.get(), BattleUnitTurnReason::TURN_QUEUE));
	if(const auto owned = initialExportSubject())
		owned->captureBranchValues(*this);
}

void HypotheticBattle::completeSwiftNormalActivation(uint32_t unitId)
{
	// Forecast consumers call this only at a completed activation boundary,
	// never for Wait/continuations; stasis passes use the separate nextTurn path.
	newHorizonsDivineMandate::completeDisciplineActivation(*this, unitId);
	const auto unit = getForUpdate(unitId);
	unit->forestAmbush.finishActivation();
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		fortuneStates.at(side).tactics.endActivation();
	if(const auto next = newHorizonsSwiftRebirth::completeNormalActivationPlan(*unit, getRound()))
		updateUnitBonus(unitId, {*next});
}
