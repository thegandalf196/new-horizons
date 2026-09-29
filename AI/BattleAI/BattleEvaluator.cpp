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
#include "BattleEvaluator.h"
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "NewHorizonsHexOfPain.h"
#include "tbb/parallel_for.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"

// TODO: remove
// Eventually only IBattleInfoCallback and battle::Unit should be used,
// CUnitState should be private and CStack should be removed completely
#include "../../lib/CStack.h"

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

enum class SpellTypes
{
	ADVENTURE, BATTLE, OTHER
};

SpellTypes spellType(const CSpell * spell)
{
	if(!spell->isCombat() || spell->isCreatureAbility())
		return SpellTypes::OTHER;

	if(spell->isOffensive() || spell->hasEffects() || spell->hasBattleEffects())
		return SpellTypes::BATTLE;

	return SpellTypes::OTHER;
}

bool isTransfigureMatter(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:transfigureMatter";
}

bool isCounterspell(const CSpell * spell)
{
	return newHorizonsMagic::isCounterspell(spell);
}

bool isCanonicalLandMine(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(spell->getId());
}

bool isSelectedQuicksand(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battle.getBattle()->getMagicRules(), spell->getId());
}

bool isCanonicalFireWall(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(spell->getId());
}

bool isCanonicalTimeStop(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

bool isCanonicalSpellLock(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL;
}

bool isPhantomArmy(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::PHANTOM_ARMY_SPELL;
}

bool isCanonicalRegeneration(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::NATURE_REGENERATION_SPELL;
}

bool isCanonicalHexOfPain(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsHexOfPainAI::SPELL_ID;
}

bool isCanonicalFrailty(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:frailty";
}

bool isHexOfPainTriggerBonus(const Bonus * bonus)
{
	if(!bonus || bonus->type != BonusType::COMBAT_EVENT_TRIGGER
		|| bonus->source != BonusSource::SPELL_EFFECT || bonus->sid.toString() != newHorizonsHexOfPainAI::SPELL_ID)
		return false;

	return bonus->subtype.toString() == newHorizonsHexOfPainAI::TRIGGER_ID;
}

float expectedMoraleActivationChange(const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->unaffectedByMorale())
		return 0.0f;

	const int morale = unit->moraleVal();
	if(morale == 0)
		return 0.0f;

	const auto settings = LIBRARY->engineSettings();
	const auto & chanceByMorale = settings->getVector(morale > 0
		? EGameSettings::COMBAT_GOOD_MORALE_CHANCE
		: EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const int diceSize = settings->getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	if(chanceByMorale.empty() || diceSize <= 0)
		return 0.0f;

	const auto chanceIndex = std::min<size_t>(static_cast<size_t>(std::abs(morale)), chanceByMorale.size()) - 1;
	const auto chance = std::max<int64_t>(0, chanceByMorale[chanceIndex]);
	const auto sign = morale > 0 ? 1.0f : -1.0f;
	return sign * static_cast<float>(chance) / static_cast<float>(diceSize);
}

int timedSpellEffectRounds(const battle::Unit * unit, SpellID spell, BonusType effectType)
{
	if(!unit)
		return 0;

	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(effectType)));
	if(!effects)
		return 0;

	int rounds = 0;
	for(const auto & effect : *effects)
		if(effect && Bonus::NTurns(effect.get()))
			rounds = std::max(rounds, static_cast<int>(effect->turnsRemain));
	return std::max(0, rounds);
}

int sorrowEffectRounds(const battle::Unit * unit)
{
	return timedSpellEffectRounds(unit, SpellID(SpellID::SORROW), BonusType::MORALE);
}

int curseEffectRounds(const battle::Unit * unit)
{
	return timedSpellEffectRounds(unit, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE);
}

float expectedSorrowTargetActivationValue(const battle::Unit * target,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || !target->alive() || target->getCount() <= 0
		|| target->isGhost() || target->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == target->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(target, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*target, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto perAttackDamage = damageCache.getDamage(target, friendly, projectedBattle);
		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto cappedPerAttackDamage = std::min(perAttackDamage, availableHealth);
		const auto attackDamage = std::min(availableHealth,
			cappedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(attackDamage <= 0)
			continue;

		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(attackDamage), damageCache, projectedBattle)));
	}
	return bestActionValue;
}

float expectedCurseTargetActivationValue(const battle::Unit * target,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || !target->alive() || target->getCount() <= 0
		|| target->isGhost() || target->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == target->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(target, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*target, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto originalPerAttackDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(target, friendly, projectedBattle));
		const auto projectedPerAttackDamage = std::max<int64_t>(0,
			damageCache.getDamage(target, friendly, projectedBattle));
		const auto originalAttackDamage = std::min(availableHealth,
			originalPerAttackDamage * static_cast<int64_t>(attackCount));
		const auto projectedAttackDamage = std::min(availableHealth,
			projectedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(originalAttackDamage <= projectedAttackDamage)
			continue;

		const auto preventedDamage = originalAttackDamage - projectedAttackDamage;
		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(preventedDamage), damageCache, projectedBattle)));
	}
	return bestActionValue;
}

float BattleEvaluator::estimateProjectedSorrowTargetValue(const battle::Unit * original, const battle::Unit * projected,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !projected->alive()
		|| original->unaffectedByMorale() || projected->unaffectedByMorale())
		return 0.0f;

	const auto remainingRounds = sorrowEffectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	// This is an expected-value estimate, not a deterministic roll forecast:
	// BattleAI cannot know the per-army seeded RandomizationBias history or all
	// future morale eligibility gates (e.g. waited/defended/fear/canMove/hadMorale).
	// Use the configured raw chance table and projected Morale delta instead of
	// recomputing Sorrow's saved-rules School-rank formula.
	const auto lostExpectedActivations = expectedMoraleActivationChange(original)
		- expectedMoraleActivationChange(projected);
	if(lostExpectedActivations <= 0.0f)
		return 0.0f;

	// One best direct attack is a bounded proxy for one normal activation. The
	// spell's real timed bonus supplies the duration; exact activation timing,
	// eligibility gates, special non-damage actions, and per-army bias history
	// remain Phase-2 integration work.
	const auto activationValue = expectedSorrowTargetActivationValue(projected, damageCache, projectedBattle);
	return lostExpectedActivations * static_cast<float>(remainingRounds) * activationValue * 0.5f;
}

float BattleEvaluator::estimateProjectedCurseTargetValue(const battle::Unit * original, const battle::Unit * projected,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive())
		return 0.0f;

	const auto remainingRounds = curseEffectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	// The projected Curse bonus drives the shared expected-damage calculator;
	// this estimator does not reproduce Curse's damage-range rule. One best
	// hostile attack is a bounded proxy for each remaining round, matching the
	// Sorrow estimate's treatment of future activations.
	const auto preventedAttackValue = expectedCurseTargetActivationValue(projected, damageCache, projectedBattle);
	return preventedAttackValue * static_cast<float>(remainingRounds) * 0.5f;
}

float BattleEvaluator::estimateProjectedFrailtyTargetValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| !projectedBattle)
		return 0.0f;

	const auto availableHealth = std::max<int64_t>(0, projected->getAvailableHealth());
	if(availableHealth <= 0)
		return 0.0f;

	int64_t increasedDamage = 0;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == projected->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(friendly, projected->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*friendly, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		// The child cache contains projected post-Frailty damage, while its parent
		// retains the live pre-cast damage snapshot. Compare one activation from
		// every allied stack, then cap their combined gain at this target's current
		// available health. This deliberately uses no guessed effect duration.
		const auto originalDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(friendly, projected, projectedBattle));
		const auto projectedDamage = std::max<int64_t>(0,
			damageCache.getDamage(friendly, projected, projectedBattle));
		if(projectedDamage <= originalDamage)
			continue;

		const auto perAttackIncrease = projectedDamage - originalDamage;
		const auto remainingHealth = availableHealth - increasedDamage;
		const auto attacksNeededToFinish = remainingHealth / attackCount
			+ (remainingHealth % attackCount == 0 ? 0 : 1);
		if(perAttackIncrease >= attacksNeededToFinish)
		{
			increasedDamage = availableHealth;
			break;
		}

		// The comparison above guarantees this product is below remainingHealth,
		// so multiplying by the bounded attack count cannot overflow int64_t.
		increasedDamage += perAttackIncrease * static_cast<int64_t>(attackCount);
	}

	if(increasedDamage <= 0)
		return 0.0f;

	return AttackPossibility::calculateDamageReduce(nullptr, projected,
		static_cast<uint64_t>(increasedDamage), damageCache, projectedBattle);
}

float BattleEvaluator::estimateProjectedHexOfPainTargetValue(const battle::Unit * original,
	const battle::Unit * projected, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| !projectedBattle || !newHorizonsHexOfPainAI::hasEffect(projected))
		return 0.0f;

	const auto remainingRounds = newHorizonsHexOfPainAI::effectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	const auto bestAttackValue = [&](DamageCache & cache)
	{
		const auto * actor = projectedBattle->battleGetUnitByID(projected->unitId());
		if(!actor || !actor->alive())
			return 0.0f;

		PotentialTargets targets(actor, cache, projectedBattle);
		if(targets.possibleAttacks.empty())
			return 0.0f;
		return std::max(0.0f, targets.possibleAttacks.front().attackValue());
	};

	// Compare the same projected position, health, and other spell effects with
	// just this registered trigger removed. The attack preview executes the Hex
	// script after each projected strike/retaliation, so this estimates the value
	// of the curse's reactive self-damage without inventing generic trigger rules.
	DamageCache hexedDamage;
	hexedDamage.buildDamageCache(projectedBattle, projected->unitSide());
	const auto hexedActionValue = bestAttackValue(hexedDamage);

	std::vector<Bonus> removedBonuses;
	const auto triggers = projected->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
		if(bonus && isHexOfPainTriggerBonus(bonus.get()))
			removedBonuses.emplace_back(*bonus);
	if(removedBonuses.empty())
		return 0.0f;

	const auto triggerSelector = CSelector([](const Bonus * bonus)
	{
		return isHexOfPainTriggerBonus(bonus);
	});
	const auto targetState = projectedBattle->getForUpdate(projected->unitId());
	targetState->removeUnitBonus(triggerSelector);

	DamageCache unhexedDamage;
	unhexedDamage.buildDamageCache(projectedBattle, projected->unitSide());
	const auto unhexedActionValue = bestAttackValue(unhexedDamage);

	// Forecasting is observational: restore the exact captured bonus objects so
	// later candidate scoring sees the accepted hypothetical cast unchanged.
	targetState->addUnitBonus(removedBonuses);

	const auto preventedActionValue = std::max(0.0f, unhexedActionValue - hexedActionValue);
	return preventedActionValue * static_cast<float>(remainingRounds) * 0.5f;
}

bool isCanonicalHolyArmor(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:holyArmor";
}

struct HolyArmorProtection
{
	int reductionPercent = 0;
	int rounds = 0;

	int exposurePercentRounds() const
	{
		return reductionPercent * rounds;
	}
};

HolyArmorProtection holyArmorProtection(const battle::Unit * unit, SpellID spell)
{
	HolyArmorProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION)));
	if(!bonuses)
		return result;
	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		const HolyArmorProtection current{
			std::clamp(bonus->val, 0, 100),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		if(current.exposurePercentRounds() > result.exposurePercentRounds())
			result = current;
	}
	return result;
}

void projectRegenerationRateSnapshot(HypotheticBattle & projectedBattle,
	const spells::Mechanics & mechanics, const CSpell * spell,
	const spells::Target & acceptedTarget)
{
	if(!isCanonicalRegeneration(spell) || acceptedTarget.size() != 1
		|| !acceptedTarget.front().unitValue)
		return;

	const auto & savedRules = projectedBattle.getBattle()->getMagicRules();
	if(!newHorizonsMagic::rulesActive(savedRules))
		return;

	const auto targetId = acceptedTarget.front().unitValue->unitId();
	auto targetState = projectedBattle.getForUpdate(targetId);
	const auto regenerationMarker = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(spell->getId())).And(Selector::type()(BonusType::HP_REGENERATION));
	if(!targetState->alive() || !targetState->hasBonus(regenerationMarker))
		return;

	const auto * hero = mechanics.getHeroCaster();
	const bool herbalist = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST));
	targetState->regenerationRateMillionths = newHorizonsMagic::regenerationRateMillionthsBasisPoints(
		std::max<int32_t>(0, mechanics.getEffectPower()),
		mechanics.getSpellPowerCoefficientBasisPoints(), herbalist,
		mechanics.getWarcastingBonusPercent());
}

int regenerationEffectRounds(const battle::Unit * unit, SpellID spell)
{
	if(!unit)
		return 0;

	int result = 0;
	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	for(const auto & effect : *effects)
		if(effect)
			result = std::max<int>(result, effect->turnsRemain);
	return std::max(0, result);
}

std::set<uint32_t> improvedRegenerationTargets(
	const HypotheticBattle & projectedBattle,
	const CBattleInfoCallback & originalBattle,
	SpellID spell)
{
	std::set<uint32_t> result;
	for(const auto * projected : projectedBattle.battleGetAllUnits())
	{
		const auto * projectedState = dynamic_cast<const battle::CUnitState *>(projected);
		if(!projectedState || !projectedState->alive() || projectedState->regenerationRateMillionths <= 0)
			continue;

		const auto * original = originalBattle.battleGetUnitByID(projected->unitId());
		const auto * originalState = dynamic_cast<const battle::CUnitState *>(original);
		const int originalRate = originalState ? originalState->regenerationRateMillionths : 0;
		const int originalRounds = regenerationEffectRounds(original, spell);
		if(projectedState->regenerationRateMillionths > originalRate
			|| regenerationEffectRounds(projected, spell) > originalRounds)
			result.insert(projected->unitId());
	}
	return result;
}

void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action)
{
	auto attackerState = state.getForUpdate(liveUnit->unitId());
	const bool attackerWasAlive = attackerState->alive();
	*attackerState = *action.attackerState;
	state.recordBloodrageTransition(attackerState, attackerWasAlive);

	if(action.defenderDamageReduce > 0)
	{
		attackerState->removeUnitBonus(Bonus::UntilAttack);
		attackerState->removeUnitBonus(Bonus::UntilOwnAttack);
	}
	if(action.attackerDamageReduce > 0)
		attackerState->removeUnitBonus(Bonus::UntilBeingAttacked);

	for(const auto & affected : action.affectedUnits)
	{
		if(!affected)
			continue;
		auto affectedState = state.getForUpdate(affected->unitId());
		const bool affectedWasAlive = affectedState->alive();
		*affectedState = *affected;
		state.recordBloodrageTransition(affectedState, affectedWasAlive);

		if(action.defenderDamageReduce > 0)
			affectedState->removeUnitBonus(Bonus::UntilBeingAttacked);
		if(action.attackerDamageReduce > 0 && action.attack.defender->unitId() == affected->unitId())
			affectedState->removeUnitBonus(Bonus::UntilAttack);
	}
}

float projectedRegenerationValue(
	const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder,
	const std::set<uint32_t> & targets,
	DamageCache & damageCache,
	BattleSide ourSide,
	PlayerColor ourPlayer,
	float positiveEffectMultiplier)
{
	if(targets.empty())
		return 0.0f;

	auto forecast = std::make_shared<HypotheticBattle>(environment, afterCast);
	auto baseline = std::make_shared<HypotheticBattle>(environment, beforeCast);
	DamageCache forecastDamage(&damageCache);
	DamageCache baselineDamage(&damageCache);
	forecastDamage.buildDamageCache(forecast, ourSide);
	baselineDamage.buildDamageCache(baseline, ourSide);
	float score = 0.0f;
	bool firstRound = true;

	for(const auto & round : turnOrder)
	{
		if(!firstRound)
		{
			forecast->nextRound();
			baseline->nextRound();
		}
		firstRound = false;

		for(const auto * queuedUnit : round)
		{
			const auto * unit = forecast->battleGetUnitByID(queuedUnit->unitId());
			const auto * baselineUnit = baseline->battleGetUnitByID(queuedUnit->unitId());
			const auto * unitState = dynamic_cast<const battle::CUnitState *>(unit);
			const auto * baselineState = dynamic_cast<const battle::CUnitState *>(baselineUnit);
			const bool startsActivation = unit
				&& forecast->battleBeginsActivation(unit, BattleUnitTurnReason::TURN_QUEUE);
			if(startsActivation && unit->alive() && targets.contains(unit->unitId())
				&& forecast->battleGetOwner(unit) == ourPlayer)
			{
				const auto projectedHeal = unitState ? unitState->regenerationProjectedHeal() : 0;
				const auto baselineHeal = baselineUnit && baselineUnit->alive() && baselineState
					? baselineState->regenerationProjectedHeal() : 0;
				const auto additionalHeal = std::max<int64_t>(0, projectedHeal - baselineHeal);
				if(additionalHeal > 0)
				{
					const auto healedValue = AttackPossibility::calculateDamageReduce(
						nullptr, unit, static_cast<uint64_t>(additionalHeal), forecastDamage, forecast);
					score += healedValue * positiveEffectMultiplier;
				}
			}

			if(unit && unit->alive())
				forecast->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			if(baselineUnit && baselineUnit->alive())
				baseline->nextTurn(baselineUnit->unitId(), BattleUnitTurnReason::TURN_QUEUE);

			const auto * currentForecastUnit = unit
				? forecast->battleGetUnitByID(unit->unitId()) : nullptr;
			const auto * currentBaselineUnit = baselineUnit
				? baseline->battleGetUnitByID(baselineUnit->unitId()) : nullptr;
			if(currentForecastUnit && currentForecastUnit->alive())
			{
				PotentialTargets potentialTargets(currentForecastUnit, forecastDamage, forecast);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*forecast, currentForecastUnit, potentialTargets.bestAction());
			}
			if(currentForecastUnit)
				forecast->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			if(currentBaselineUnit && currentBaselineUnit->alive())
			{
				PotentialTargets potentialTargets(currentBaselineUnit, baselineDamage, baseline);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*baseline, currentBaselineUnit, potentialTargets.bestAction());
			}
			if(currentBaselineUnit)
				baseline->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
		}
	}

	return score;
}

template<typename Unit>
int64_t phantomArmyMarkerIntegrity(const Unit * unit, SpellID spellId)
{
	const auto markers = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellId)));
	if(!markers)
		return 0;

	for(const auto & marker : *markers)
		if(marker && marker->type == BonusType::NONE)
			return std::max<int64_t>(0, marker->val);

	return 0;
}

template<typename Unit>
int64_t phantomArmyCurrentIntegrity(const Unit * unit, SpellID spellId)
{
	if constexpr(requires { unit->getPhantomIntegrity(); })
		return std::max<int64_t>(0, unit->getPhantomIntegrity());
	else
		return phantomArmyMarkerIntegrity(unit, spellId);
}

template<typename Unit>
int64_t phantomArmyInitialIntegrity(const Unit * unit, SpellID spellId)
{
	if constexpr(requires { unit->getPhantomInitialIntegrity(); })
		return std::max<int64_t>(0, unit->getPhantomInitialIntegrity());
	else
		return phantomArmyMarkerIntegrity(unit, spellId);
}

/// Estimate the temporary combat value of one newly projected Phantom Army
/// stack.  Its copied count provides normal offensive power, while the
/// integrity pool limits how much of that power is likely to survive; its
/// two-round lifetime caps the contribution.  This score deliberately uses
/// the Phantom profile instead of the stack's synthetic ordinary health.
float phantomArmyCombatValue(const battle::Unit * unit, SpellID spellId)
{
	const auto currentIntegrity = phantomArmyCurrentIntegrity(unit, spellId);
	const auto initialIntegrity = phantomArmyInitialIntegrity(unit, spellId);
	const auto totalHealth = std::max<int64_t>(1, unit->getTotalHealth());
	const auto creature = unit->unitType();
	if(currentIntegrity <= 0 || initialIntegrity <= 0 || !creature || unit->getCount() <= 0)
		return 0.0f;

	const auto integrityFraction = std::clamp(
		static_cast<float>(initialIntegrity) / static_cast<float>(totalHealth), 0.0f, 1.0f);
	const auto survivingFraction = std::clamp(
		static_cast<float>(currentIntegrity) / static_cast<float>(initialIntegrity), 0.0f, 1.0f);
	const auto expectedActiveRounds = static_cast<float>(newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS)
		* survivingFraction;
	const auto copiedArmyValue = static_cast<float>(unit->getCount())
		* static_cast<float>(std::max(0, creature->getAIValue()));

	return copiedArmyValue * integrityFraction * expectedActiveRounds;
}

BattleHex::EDir fireWallDirection(const spells::Target & target)
{
	if(target.size() < 2 || target.front().unitValue != nullptr || target.at(1).unitValue != nullptr)
		return BattleHex::NONE;

	for(const auto direction : BattleHex::hexagonalDirections())
		if(target.front().hexValue.cloneInDirection(direction, false) == target.at(1).hexValue)
			return direction;

	return BattleHex::NONE;
}

/// Estimate the deterministic value of arming Counterspell without mutating a
/// battle preview.  The authoritative battle snapshot is used for the enemy
/// hero, mana costs, and current ward state; the enemy spellbook is only used
/// as a read-only threat list.  A ward is useful only when the AI can afford
/// both the arming cast and at least one likely enemy spell's listed ward cost.
float counterspellThreatValue(const CBattleInfoCallback & battle, BattleSide side,
	const CGHeroInstance * caster, const CSpell * counterspell)
{
	if(!caster || !counterspell || battle.battleWasCounterspellArmed(side))
		return 0.0f;

	const auto enemySide = CBattleInfoEssentials::otherSide(side);
	const auto * enemy = battle.getBattle()->getSideHero(enemySide);
	if(!enemy || !enemy->hasSpellbook())
		return 0.0f;

	// Player callbacks intentionally hide enemy hero details.  Spell-level
	// blockers must nevertheless be evaluated from the all-knowing authoritative
	// battle callback, rather than from the caller's perspective-limited view.
	const auto * authoritativeBattle = dynamic_cast<const CBattleInfoCallback *>(battle.getBattle());
	if(!authoritativeBattle)
		return 0.0f;
	const auto minEnemySpellLevel = authoritativeBattle->battleMinSpellLevel(enemySide);
	const auto maxEnemySpellLevel = authoritativeBattle->battleMaxSpellLevel(enemySide);

	const int armCost = battle.battleGetSpellCost(counterspell, caster);
	const int64_t remainingMana = caster->getManaAvailable() - armCost;
	if(remainingMana < 0)
		return 0.0f;

	const bool countermage = caster->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage");
	float bestValue = 0.0f;
	for(const auto spellID : enemy->getInscribedSpellsForCasting())
	{
		const auto * spell = spellID.toSpell();
		if(!spell || !spell->isCombat() || spell->isCreatureAbility() || isCounterspell(spell))
			continue;
		const int spellLevel = battle.battleGetSpellLevel(spell->getId());
		if(spellLevel < minEnemySpellLevel || spellLevel > maxEnemySpellLevel)
			continue;
		if(!enemy->canCastThisSpell(spell))
			continue;

		const int listedCost = enemy->getListedSpellCost(spell);
		const int enemyManaCost = battle.battleGetSpellCost(spell, enemy);
		if(listedCost < 0 || enemy->getManaAvailable() < enemyManaCost)
			continue;

		const int wardCost = newHorizonsMagic::counterspellCost(listedCost, countermage);
		if(remainingMana < wardCost)
			continue;

		// A high-cost offensive spell represents the largest deterministic
		// threat.  The small non-offensive component still lets the ward cover
		// decisive disables, buffs, and summons without making it automatic.
		const float value = static_cast<float>(listedCost) * 100.0f
			+ (spell->isOffensive() ? 250.0f : 0.0f)
			- static_cast<float>(armCost) * 10.0f;
		bestValue = std::max(bestValue, value);
	}

	return bestValue;
}

namespace
{
constexpr HeroCommand riposteCommand()
{
	return HeroCommand::RIPOSTE;
}

constexpr HeroCommand braceCommand()
{
	return HeroCommand::BRACE;
}

constexpr HeroCommand protectCommand()
{
	return HeroCommand::PROTECT;
}

constexpr HeroCommand flankCommand()
{
	return HeroCommand::FLANK;
}

constexpr HeroCommand secondWindCommand()
{
	return HeroCommand::SECOND_WIND;
}

bool isEligibleOrderUnit(const CBattleInfoCallback & battle, BattleSide side,
	const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && unit->isValidTarget()
		&& battle.battleGetOwner(unit) == battle.sideToPlayer(side)
		&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool adjacentForProtect(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second || !first->getPosition().isValid() || !second->getPosition().isValid())
		return false;
	for(const auto & firstHex : first->getHexes())
		for(const auto & secondHex : second->getHexes())
			if(BattleHex::getDistance(firstHex, secondHex) <= 1)
				return true;
	return false;
}

/// A value-only fallback for older snapshots and for runtime APIs that expose
/// only the ordinary one-target query.  It is intentionally conservative: the
/// authoritative callback remains the final arbiter before an action is sent.
template <typename Battle>
std::vector<std::vector<uint32_t>> orderTargetOptions(const Battle & battle,
	BattleSide side, HeroCommand command)
{
	std::vector<std::vector<uint32_t>> result;

	// Canonical runtime builds validate the complete target tuple (including the
	// Protector/Ward ordering and round eligibility) through this snapshot API.
	// Enumerate only the small candidate identity set exposed by the callback;
	// never infer authority from local geometry or activation flags.
	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, std::vector<uint32_t>{}); })
	{
		// The method is present on every current callback, including legacy
		// targeted-command snapshots.  Only the canonical ruleset gives it
		// authority; legacy Focus Fire still uses its scalar validator below.
		const bool canonicalRules = battle.getBattle()
			&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		if(canonicalRules)
		{
			const auto candidates = battle.battleGetHeroCommandTargets(side, command);
			if(command == protectCommand())
			{
				for(const auto protector : candidates)
					for(const auto ward : candidates)
						if(protector != ward && battle.battlePrepareHeroOrderState(side, command, {protector, ward}))
							result.push_back({protector, ward});
			}
			else
			{
				for(const auto targetId : candidates)
					if(battle.battlePrepareHeroOrderState(side, command, {targetId}))
						result.push_back({targetId});
			}
			return result;
		}
	}

	if(command == protectCommand())
	{
		const auto units = battle.battleGetAllUnits(false);
		for(size_t i = 0; i < units.size(); ++i)
		{
			if(!isEligibleOrderUnit(battle, side, units[i]))
				continue;
			for(size_t j = 0; j < units.size(); ++j)
			{
				if(i != j && isEligibleOrderUnit(battle, side, units[j]) && adjacentForProtect(units[i], units[j]))
					result.push_back({units[i]->unitId(), units[j]->unitId()});
			}
		}
		return result;
	}

	// The existing callback already returns authoritative Focus Fire targets;
	// the runtime extension uses the same shape for Flank and Second Wind.
	for(const auto targetId : battle.battleGetHeroCommandTargets(side, command))
		result.push_back({targetId});
	return result;
}

template <typename Battle>
bool commandTargetIsLegal(const Battle & battle, BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targetIds)
{
	if(targetIds.empty())
		return battle.battleCanUseHeroCommand(side, command);

	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, targetIds); })
	{
		if(battle.getBattle() && heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
			return battle.battlePrepareHeroOrderState(side, command, targetIds).has_value();
	}
	if constexpr(requires { battle.battleGetHeroCommandTargetOptions(side, command); })
	{
		for(const auto & option : battle.battleGetHeroCommandTargetOptions(side, command))
			if(std::vector<uint32_t>(option.begin(), option.end()) == targetIds)
				return true;
		return false;
	}
	else if constexpr(requires { battle.battleCanConfirmHeroCommand(side, command, targetIds); })
		return battle.battleCanConfirmHeroCommand(side, command, targetIds);
	else if(targetIds.size() == 1)
		return battle.battleCanConfirmHeroCommand(side, command, targetIds.front());
	else
		// Protect's pair legality is checked atomically by the new server API.
		// No pair must be sent from a legacy callback that cannot validate it.
		return false;
}

float averageOrderDamage(const DamageEstimation & damage)
{
	return static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
}

/// Estimate visible creature spell pressure without querying a concealed enemy
/// hero or asking a random-spellcaster to choose an ability. Spellcaster
/// bonuses are part of the visible creature stack; each stack contributes its
/// strongest available direct-damage spell once. `magicalOnly` is for defenses
/// that reduce magical damage but do not affect physical attacks. Do not call
/// `canBeCast` here: a player-specific callback may refuse to answer for an
/// opposing side, which is not evidence that its visible spellcaster is inert.
/// `canCast()` and explicit visible SPELLCASTER bonuses bound this estimate.
float visibleCreatureSpellThreat(
	const std::vector<const battle::Unit *> & enemyUnits, bool magicalOnly = false)
{
	float totalThreat = 0.0f;
	for(const auto * enemy : enemyUnits)
	{
		if(!enemy || !enemy->canCast())
			continue;
		float bestSpellDamage = 0.0f;
		const auto spellcasters = enemy->getBonuses(Selector::type()(BonusType::SPELLCASTER));
		if(!spellcasters)
			continue;
		for(const auto & bonus : *spellcasters)
		{
			if(!bonus || bonus->parameters || !bonus->subtype.as<SpellID>().hasValue())
				continue;
			const auto * spell = bonus->subtype.as<SpellID>().toSpell();
			if(!spell || !spell->isCombat()
				|| (!spell->isOffensive() && !spell->isDamage())
				|| spell->isCreatureAbility()
				|| (magicalOnly && !spell->isMagical()))
				continue;
			const auto spellDamage = static_cast<float>(std::max<int64_t>(0, spell->calculateDamage(enemy)));
			bestSpellDamage = std::max(bestSpellDamage, spellDamage);
		}
		totalThreat += bestSpellDamage;
	}
	return totalThreat;
}

float holyArmorMitigationValue(BattleSide side,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget, SpellID spell,
	int64_t friendlyHealth, float visibleMagicalThreat, DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> projectedBattle)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->unitSide() != side || visibleMagicalThreat <= 0.0f || friendlyHealth <= 0)
		return 0.0f;

	const auto targetHealth = liveTarget->getAvailableHealth();
	if(targetHealth <= 0)
		return 0.0f;

	const auto before = holyArmorProtection(liveTarget, spell);
	const auto after = holyArmorProtection(projectedTarget, spell);
	const int addedExposure = after.exposurePercentRounds() - before.exposurePercentRounds();
	if(addedExposure <= 0)
		return 0.0f;

	// Spread the visible creature-caster volley across allied health as a bounded
	// target-priority estimate. The detached cast supplies the actual Holy Armor
	// reduction and duration; the threat side deliberately uses one best spell
	// per visible caster and does not infer anything from an enemy hero's book.
	const long double targetShare = static_cast<long double>(targetHealth)
		/ static_cast<long double>(friendlyHealth);
	const long double estimatedSavedDamage = std::min(
		static_cast<long double>(targetHealth),
		static_cast<long double>(visibleMagicalThreat) * targetShare
			* static_cast<long double>(addedExposure) / 100.0L);
	if(!std::isfinite(estimatedSavedDamage) || estimatedSavedDamage <= 0.0L)
		return 0.0f;

	const auto savedDamage = static_cast<uint64_t>(std::floor(estimatedSavedDamage));
	if(savedDamage == 0)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, liveTarget, savedDamage, damageCache, std::move(projectedBattle));
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;
	const auto maxHealth = liveTarget->getMaxHealth();
	const auto creatureValue = liveTarget->unitType()->getAIValue();
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	// The usual damage-reduction value reflects the attacks the saved stack can
	// still make. That can be zero for a distant or temporarily immobilized unit,
	// even though preserving its health is useful. Fall back to a bounded share
	// of the stack's creature value so defensive spells are not treated as no-ops.
	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	const auto boundedHealthValue = static_cast<float>(std::min(
		healthValue, static_cast<long double>(std::numeric_limits<float>::max())));
	return boundedHealthValue;
}

/// Hero presence is exposed through InfoAboutHero even when the opposing
/// hero's detailed state is concealed.  Use only that public presence bit and
/// the allied army's visible health to represent a modest possible hero spell;
/// never inspect the hidden hero's book, mana, attributes, or perks.
float publicEnemyHeroSpellThreat(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<const battle::Unit *> & ownUnits)
{
	const auto enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const auto enemyHero = battle.battleGetHeroInfo(enemySide);
	if(enemyHero.owner == PlayerColor::NEUTRAL)
		return 0.0f;

	int64_t alliedHealth = 0;
	for(const auto * own : ownUnits)
		if(own)
			alliedHealth += own->getAvailableHealth();

	// Estimate at most 4% of current allied health and cap it so the public
	// presence signal remains a modest prior rather than overwhelming real
	// spell or attack valuations.
	return std::min(static_cast<float>(alliedHealth) * 0.04f, 500.0f);
}

float canonicalOrderHeuristic(const CBattleInfoCallback & battle, BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetIds)
{
	const auto perspective = battle.battleGetMySide();
	const bool heroKnown = perspective == BattleSide::ALL_KNOWING || perspective == side;
	const auto * hero = heroKnown ? battle.battleGetFightingHero(side) : nullptr;
	const auto units = battle.battleGetAllUnits(false);
	const auto ownPlayer = battle.sideToPlayer(side);
	std::vector<const battle::Unit *> ownUnits;
	std::vector<const battle::Unit *> enemyUnits;
	for(const auto * unit : units)
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
			continue;
		if(battle.battleGetOwner(unit) == ownPlayer)
			ownUnits.push_back(unit);
		else
			enemyUnits.push_back(unit);
	}

	const auto & commandRules = battle.getBattle()->getHeroCommandRules()["commands"];
	const int warcastingBonus = hero && newHorizonsWarcasting::enabled(battle.getBattle()->getMagicRules())
		? newHorizonsWarcasting::orderBonus(battle.getBattle()->getWarcastingState(side), battle.battleGetRound()) : 0;
	const auto coefficient = [&](const char * commandKey, const char * effectKey)
	{
		const auto & formula = commandRules[commandKey]["effects"][effectKey];
		return static_cast<float>(hero ? heroCommands::coefficient(formula, *hero, warcastingBonus)
			: heroCommands::coefficient(formula, 0, 0));
	};
	const auto meleeDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit || !attackerUnit->isMeleeAttacker())
			return 0.0f;
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, false)));
	};
	const auto anyDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit)
			return 0.0f;
		const bool shooting = battle.battleCanShoot(attackerUnit, defenderUnit->getPosition());
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, shooting)));
	};
	const auto bestOwnMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : ownUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto bestEnemyMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : enemyUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto sumOwnMeleeDamage = [&]
	{
		float total = 0.0f;
		for(const auto * own : ownUnits)
		{
			float best = 0.0f;
			for(const auto * enemy : enemyUnits)
				best = std::max(best, meleeDamage(own, enemy));
			total += best;
		}
		return total;
	};
	const auto ownMeleePotential = sumOwnMeleeDamage();
	if(command == HeroCommand::FOCUS_FIRE && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		const auto rangedPercent = coefficient("focusFire", "rangedDamagePercent");
		float rangedPotential = 0.0f;
		for(const auto * unit : ownUnits)
		{
			if(!unit->isShooter() || unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				|| !unit->willMove(0) || !battle.battleCanShoot(unit, target->getPosition()))
				continue;
			rangedPotential += anyDamage(unit, target);
		}
		return rangedPotential * rangedPercent / 100.0f;
	}
	const auto chargePercent = coefficient("charge", "meleeDamagePercent");
	const auto holdPercent = coefficient("holdTheLine", "damageReductionPercent");
	const auto riposteReduction = coefficient("riposte", "meleeDamageReductionPercent");
	const auto riposteDamage = coefficient("riposte", "retaliationDamagePercent");
	const auto braceDamage = coefficient("brace", "preemptiveDamagePercent");
	const auto protectReduction = coefficient("protect", "interceptedDamageReductionPercent");
	const auto flankDamage = coefficient("flank", "meleeDamagePercent");

	if(command == HeroCommand::CHARGE)
	{
		const bool canCharge = std::any_of(ownUnits.begin(), ownUnits.end(), [](const auto * unit)
		{
			return unit->isMeleeAttacker() && unit->getMovementRange(0) >= 3;
		});
		return canCharge ? ownMeleePotential * chargePercent / 100.0f : 0.0f;
	}

	if(command == HeroCommand::HOLD_THE_LINE)
	{
		float incoming = 0.0f;
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
				if(enemy->isMeleeAttacker())
					incoming = std::max(incoming, meleeDamage(enemy, own));
		float value = incoming * holdPercent / 100.0f;
		if(hero && hero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
			newHorizonsIronDiscipline::PERK))
		{
			// Use the exact saved reduction that issuing Hold would snapshot.  The
			// prepare query is read-only.  Threat estimation uses only public basic
			// enemy-hero presence, visible allied health and visible creature spells.
			const auto prepared = battle.battlePrepareHeroOrderState(side, command, {});
			if(prepared && prepared->holdMagicalReductionBasisPoints > 0)
			{
				const auto visibleCreatureThreat = visibleCreatureSpellThreat(enemyUnits, true);
				const auto publicHeroThreat = publicEnemyHeroSpellThreat(battle, side, ownUnits);
				const auto magicalThreat = std::max(visibleCreatureThreat, publicHeroThreat);
				value += magicalThreat * static_cast<float>(prepared->holdMagicalReductionBasisPoints) / 10000.0f;
			}
		}
		return value;
	}

	if(command == riposteCommand())
	{
		float incoming = 0.0f;
		float retaliation = 0.0f;
		float vengeanceRetaliation = 0.0f;
		static const auto firstStrikeSelector = Selector::typeSubtype(
			BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
			{
				if(!enemy->isMeleeAttacker())
					continue;
				const BattleAttackInfo incomingAttack(enemy, own, 0, false);
				DamageEstimation retaliationEstimate;
				const auto incomingEstimate = battle.battleEstimateDamage(incomingAttack, &retaliationEstimate);
				incoming = std::max(incoming, averageOrderDamage(incomingEstimate));
				retaliation = std::max(retaliation, averageOrderDamage(retaliationEstimate));
				if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE)
					&& isEligibleOrderUnit(battle, side, own)
					&& own->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
					&& !own->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS)
					&& !own->isTimeStopped()
					&& !own->hasBonusOfType(BonusType::NO_RETALIATION)
					&& !enemy->hasBonusOfType(BonusType::BLOCKS_RETALIATION)
					&& !enemy->isInvincible()
					&& !battle.isLongWeaponAttack(enemy, own)
					&& (!battle.battleShroudDeniesRetaliation(incomingAttack)
						|| own->hasBonus(firstStrikeSelector))
					&& !newHorizonsOffense::hasVengeanceRetaliationBonus(own))
				{
					// Vengeance supplies the charge that ableToRetaliate() would otherwise
					// reject after a spent counter. Mirror the engine's non-ammunition
					// restrictions above, then estimate from detached post-hit states.
					const auto estimateExtraCounter = [&](int64_t projectedDamage)
					{
						auto projectedOwn = own->acquireState();
						projectedOwn->damage(projectedDamage);
						if(!projectedOwn->alive())
							return DamageEstimation();
						BattleAttackInfo extraRetaliation(projectedOwn.get(), enemy, 0, false);
						extraRetaliation.retaliation = true;
						return battle.battleEstimateDamage(extraRetaliation);
					};
					const auto counterAfterMinimumDamage = estimateExtraCounter(incomingEstimate.damage.min);
					const auto counterAfterMaximumDamage = estimateExtraCounter(incomingEstimate.damage.max);
					DamageEstimation projectedRetaliation;
					projectedRetaliation.damage.min = std::min(counterAfterMinimumDamage.damage.min,
						counterAfterMaximumDamage.damage.min);
					projectedRetaliation.damage.max = std::max(counterAfterMinimumDamage.damage.max,
						counterAfterMaximumDamage.damage.max);
					projectedRetaliation.kills.min = std::min(counterAfterMinimumDamage.kills.min,
						counterAfterMaximumDamage.kills.min);
					projectedRetaliation.kills.max = std::max(counterAfterMinimumDamage.kills.max,
						counterAfterMaximumDamage.kills.max);
					vengeanceRetaliation = std::max(vengeanceRetaliation,
						averageOrderDamage(projectedRetaliation));
				}
			}
		const float extraRetaliation = vengeanceRetaliation * (100.0f + riposteDamage) / 100.0f;
		return incoming * riposteReduction / 100.0f
			+ retaliation * riposteDamage / 100.0f + extraRetaliation;
	}

	if(command == braceCommand())
	{
		float advancingDamage = 0.0f;
		for(const auto * enemy : enemyUnits)
			if(enemy->isMeleeAttacker() && enemy->getMovementRange(0) >= 3)
				for(const auto * own : ownUnits)
					// Brace is a pre-emptive blow by the defending friendly stack,
					// not a multiplier on the advancing enemy's own attack.
					advancingDamage = std::max(advancingDamage, meleeDamage(own, enemy));
		const auto resolvedBraceDamage = newHorizonsCombatSkills::bracePreemptivePercent(braceDamage, hero);
		return advancingDamage * resolvedBraceDamage / 100.0f;
	}

	if(command == protectCommand() && targetIds.size() == 2)
	{
		const auto * protector = battle.battleGetUnitByID(targetIds[0]);
		const auto * ward = battle.battleGetUnitByID(targetIds[1]);
		if(!isEligibleOrderUnit(battle, side, protector) || !isEligibleOrderUnit(battle, side, ward))
			return 0.0f;
		return bestEnemyMeleeDamage(ward) * protectReduction / 100.0f;
	}

	if(command == flankCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		// Estimate the immediate opportunity from allied melee stacks that have not
		// acted yet. The authoritative attack path records exact sides as attacks
		// resolve; this projection reads current contacts only and never mutates them.
		uint8_t availableSides = 0;
		for(const auto * unit : ownUnits)
		{
			if(!unit->isMeleeAttacker() || !unit->willMove(0))
				continue;
			availableSides |= battle.battleHeroOrderFlankSide(unit, target);
		}
		int distinctSides = 0;
		for(auto bits = availableSides; bits; bits &= static_cast<uint8_t>(bits - 1))
			++distinctSides;
		const int additionalSides = std::max(0, distinctSides - 1);
		const int additionalSidePercent = battle.battleHeroOrderFlankAdditionalSidePercent(side, warcastingBonus);
		return bestOwnMeleeDamage(target)
			* (flankDamage + additionalSides * additionalSidePercent) / 100.0f;
	}

	if(command == secondWindCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!isEligibleOrderUnit(battle, side, target) || !target->moved(0))
			return 0.0f;
		float extraAttack = 0.0f;
		for(const auto * enemy : enemyUnits)
			extraAttack = std::max(extraAttack, anyDamage(target, enemy));
		const auto directDamagePercent = hero
			? static_cast<float>(heroCommands::secondWindPercent(*hero, warcastingBonus)) : 50.0f;
		return extraAttack * directDamagePercent / 100.0f;
	}

	return 0.0f;
}

bool defensiveStanceMakesDefendWorthwhile(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & battle, const battle::Unit * stack)
{
	if(!stack || stack->defended()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack))
		return false;
	const auto * hero = battle->battleGetOwnerHero(stack);
	const int bulwarkRank = newHorizonsBulwark::rank(hero);
	const int paviseReduction = newHorizonsCombatSkills::paviseReductionPercent(hero);
	if(bulwarkRank == 0 && paviseReduction == 0)
		return false;

	auto defendedPreview = std::make_shared<HypotheticBattle>(environment, battle);
	auto projectedTarget = defendedPreview->getForUpdate(stack->unitId());
	if(!projectedTarget)
		return false;
	projectedTarget->defending = true;
	struct IncomingThreat
	{
		const battle::Unit * enemy = nullptr;
		bool shooting = false;
		bool physicalDamage = true;
		int attackCount = 1;
		float beforePerAttack = 0.0f;
		float afterFirstPerAttack = 0.0f;
		float afterPerAttack = 0.0f;
		std::optional<int32_t> immovableRoundBefore;
	};
	std::vector<IncomingThreat> threats;
	struct SharedCoverThreat
	{
		const battle::Unit * ally = nullptr;
		float beforeTotal = 0.0f;
		float afterTotal = 0.0f;
		bool immovableAvailable = false;
	};
	std::vector<SharedCoverThreat> sharedCoverThreats;
	if(newHorizonsBulwark::hasSharedCover(hero))
	{
		for(const auto * ally : battle->battleAdjacentUnits(stack))
			if(ally && ally->unitSide() == stack->unitSide() && ally->defended()
				&& ally->alive() && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(ally))
			{
				const auto * allyHero = battle->battleGetOwnerHero(ally);
				const auto allyState = ally->acquireState();
				const bool immovableAvailable = newHorizonsBulwark::hasImmovable(allyHero) && allyState
					&& allyState->bulwarkImmovableRound != battle->battleGetRound();
				sharedCoverThreats.push_back({ally, 0.0f, 0.0f, immovableAvailable});
			}
	}
	const auto enemies = battle->battleGetUnitsIf([&](const battle::Unit * enemy)
	{
		return enemy && enemy->alive()
			&& battle->battleGetOwner(enemy) != battle->battleGetOwner(stack)
			&& !enemy->isGhost()
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(enemy);
	});
	bool immovableForecastAvailable = newHorizonsBulwark::hasImmovable(hero)
		&& projectedTarget->bulwarkImmovableRound != battle->battleGetRound();
	for(const auto * enemy : enemies)
	{
		bool canShoot = enemy->canShoot()
			&& battle->battleCanShoot(enemy, stack->getPosition());
		bool canMelee = false;
		if(enemy->isMeleeAttacker())
		{
			canMelee = !battle->meleeAttackHexes(enemy, stack, enemy->getPosition()).empty();
			if(!canMelee)
				for(const auto & position : battle->battleGetAvailableHexes(enemy, false))
					if(!battle->meleeAttackHexes(enemy, stack, position).empty())
					{
						canMelee = true;
						break;
					}
		}
		// An enemy that can either shoot or close to melee chooses one attack
		// mode. Forecast the more damaging immediate option instead of counting
		// the same stack as two separate threats.
		const auto estimate = [&](bool shooting)
		{
			const BattleAttackInfo beforeAttack(enemy, stack, 0, shooting);
			const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
			const BattleAttackInfo afterAttack(projectedEnemy, projectedTarget.get(), 0, shooting);
			return std::pair{averageOrderDamage(battle->battleEstimateDamage(beforeAttack)),
				averageOrderDamage(defendedPreview->battleEstimateDamage(afterAttack))};
		};
		std::optional<IncomingThreat> selected;
		for(const bool shooting : {false, true})
		{
			if((shooting && !canShoot) || (!shooting && !canMelee))
				continue;
			const auto [before, after] = estimate(shooting);
			const int count = std::max(0, AttackPossibility::getAttackCount(*enemy, shooting, *battle));
			if(count <= 0)
				continue;
			IncomingThreat candidate{enemy, shooting,
				!shooting || !enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK), count,
				before, after, after, std::nullopt};
			if(!selected || candidate.beforePerAttack * candidate.attackCount
				> selected->beforePerAttack * selected->attackCount)
				selected = candidate;
		}
		if(selected)
		{
			threats.push_back(*selected);
			auto & threat = threats.back();
			if(immovableForecastAvailable && threat.physicalDamage)
			{
				// Keep the first hit at the enhanced value, then consume Immovable
				// in this detached round projection before estimating follow-up hits.
				threat.immovableRoundBefore = projectedTarget->bulwarkImmovableRound;
				projectedTarget->bulwarkImmovableRound = battle->battleGetRound();
				const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
				const BattleAttackInfo subsequentAttack(projectedEnemy, projectedTarget.get(), 0, threat.shooting);
				threat.afterPerAttack = averageOrderDamage(
					defendedPreview->battleEstimateDamage(subsequentAttack));
				immovableForecastAvailable = false;
			}
		}

		// Shared Cover only protects a neighboring stack that is already
		// Defending. Forecast immediate shots and adjacent melee attacks here;
		// avoid another reachable-hex search for every neighbor and enemy.
		for(auto & sharedThreat : sharedCoverThreats)
		{
			bool allyCanShoot = enemy->canShoot();
			if(allyCanShoot)
			{
				allyCanShoot = false;
				for(const auto hex : sharedThreat.ally->getHexes())
					if(battle->battleCanShoot(enemy, hex))
					{
						allyCanShoot = true;
						break;
					}
			}
			const bool allyCanMelee = enemy->isMeleeAttacker()
				&& !battle->meleeAttackHexes(enemy, sharedThreat.ally, enemy->getPosition()).empty();
			if(!allyCanShoot && !allyCanMelee)
				continue;

			std::optional<IncomingThreat> allyThreat;
			for(const bool shooting : {false, true})
			{
				if((shooting && !allyCanShoot) || (!shooting && !allyCanMelee))
					continue;
				const BattleAttackInfo beforeAttack(enemy, sharedThreat.ally, 0, shooting);
				const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
				const auto * projectedAlly = defendedPreview->battleGetUnitByID(sharedThreat.ally->unitId());
				const BattleAttackInfo afterAttack(projectedEnemy, projectedAlly, 0, shooting);
				const int count = std::max(0, AttackPossibility::getAttackCount(*enemy, shooting, *battle));
				if(count <= 0)
					continue;
				const float after = averageOrderDamage(defendedPreview->battleEstimateDamage(afterAttack));
				IncomingThreat candidate{enemy, shooting,
					!shooting || !enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK), count,
					averageOrderDamage(battle->battleEstimateDamage(beforeAttack)),
					after, after, std::nullopt};
				if(!allyThreat || candidate.beforePerAttack * candidate.attackCount
					> allyThreat->beforePerAttack * allyThreat->attackCount)
					allyThreat = candidate;
			}
			if(allyThreat)
			{
				sharedThreat.beforeTotal += allyThreat->beforePerAttack * allyThreat->attackCount;
				if(sharedThreat.immovableAvailable && allyThreat->physicalDamage)
				{
					auto projectedAlly = defendedPreview->getForUpdate(sharedThreat.ally->unitId());
					projectedAlly->bulwarkImmovableRound = battle->battleGetRound();
					const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
					const BattleAttackInfo subsequentAttack(projectedEnemy, projectedAlly.get(), 0,
						allyThreat->shooting);
					const float afterPerAttack = averageOrderDamage(
						defendedPreview->battleEstimateDamage(subsequentAttack));
					sharedThreat.afterTotal += allyThreat->afterFirstPerAttack
						+ afterPerAttack * std::max(0, allyThreat->attackCount - 1);
					sharedThreat.immovableAvailable = false;
				}
				else
					sharedThreat.afterTotal += allyThreat->afterFirstPerAttack
						+ allyThreat->afterPerAttack * std::max(0, allyThreat->attackCount - 1);
			}
		}
	}

	float preemptiveValue = 0.0f;
	const int preemptivePercent = newHorizonsBulwark::preemptivePercent(
		bulwarkRank, newHorizonsBulwark::hasBogAmbush(hero));
	if(preemptivePercent > 0 && !projectedTarget->bulwarkPreemptiveUsed)
	{
		const IncomingThreat * bestMeleeThreat = nullptr;
		float bestReaction = 0.0f;
		for(const auto & threat : threats)
		{
			if(threat.shooting || !threat.physicalDamage)
				continue;
			auto projectedEnemy = defendedPreview->getForUpdate(threat.enemy->unitId());
			BattleAttackInfo reaction(projectedTarget.get(), projectedEnemy.get(), 0, false);
			reaction.retaliation = true;
			reaction.preemptiveDamagePercent = preemptivePercent;
			const float value = averageOrderDamage(defendedPreview->battleEstimateDamage(reaction));
			if(value > bestReaction)
			{
				bestReaction = value;
				bestMeleeThreat = &threat;
			}
		}
		if(bestMeleeThreat)
		{
			const IncomingThreat & threat = *bestMeleeThreat;
			auto projectedEnemy = defendedPreview->getForUpdate(threat.enemy->unitId());
			BattleAttackInfo reaction(projectedTarget.get(), projectedEnemy.get(), 0, false);
			reaction.retaliation = true;
			reaction.preemptiveDamagePercent = preemptivePercent;
			auto reactionDamage = defendedPreview->battleExpectedLuckDamage(reaction);
			vstd::amin(reactionDamage, projectedEnemy->getAvailableHealth());
			preemptiveValue = static_cast<float>(reactionDamage);
			projectedEnemy->damage(reactionDamage);
			if(!projectedEnemy->alive())
			{
				for(auto & projectedThreat : threats)
					if(projectedThreat.enemy->unitId() == threat.enemy->unitId()
						&& !projectedThreat.shooting)
					{
						projectedThreat.afterFirstPerAttack = 0.0f;
						projectedThreat.afterPerAttack = 0.0f;
					}
			}
			else
			{
				BattleAttackInfo afterReaction(projectedEnemy.get(), projectedTarget.get(), 0, false);
				const float afterPerAttack = averageOrderDamage(
					defendedPreview->battleEstimateDamage(afterReaction));
				float afterFirstPerAttack = afterPerAttack;
				if(threat.immovableRoundBefore)
				{
					const auto consumedRound = projectedTarget->bulwarkImmovableRound;
					projectedTarget->bulwarkImmovableRound = *threat.immovableRoundBefore;
					BattleAttackInfo firstAfterReaction(projectedEnemy.get(), projectedTarget.get(), 0, false);
					afterFirstPerAttack = averageOrderDamage(
						defendedPreview->battleEstimateDamage(firstAfterReaction));
					projectedTarget->bulwarkImmovableRound = consumedRound;
				}
				// The attacker's damage count may change after the reaction, which
				// also changes how much physical damage can be reflected.
				for(auto & projectedThreat : threats)
					if(projectedThreat.enemy->unitId() == threat.enemy->unitId()
						&& !projectedThreat.shooting)
					{
						projectedThreat.afterFirstPerAttack = afterFirstPerAttack;
						projectedThreat.afterPerAttack = afterPerAttack;
					}
			}
			projectedTarget->bulwarkPreemptiveUsed = true;
		}
	}
	float incomingBeforeDefend = 0.0f;
	float incomingAfterDefend = 0.0f;
	for(const auto & threat : threats)
	{
		incomingBeforeDefend += threat.beforePerAttack * threat.attackCount;
		incomingAfterDefend += threat.afterFirstPerAttack
			+ threat.afterPerAttack * std::max(0, threat.attackCount - 1);
	}
	const float currentHealth = static_cast<float>(stack->getAvailableHealth());
	const float damagePrevented = std::max(0.0f,
		std::min(currentHealth, incomingBeforeDefend) - std::min(currentHealth, incomingAfterDefend));

	int64_t remainingHealth = stack->getAvailableHealth();
	int64_t reflectedDamage = 0;
	int64_t swampRenewalDamage = 0;
	for(const auto & threat : threats)
	{
		if(remainingHealth <= 0)
			break;
		const float proposed = threat.afterFirstPerAttack
			+ threat.afterPerAttack * std::max(0, threat.attackCount - 1);
		const int64_t received = std::min<int64_t>(remainingHealth,
			static_cast<int64_t>(std::max(0.0f, proposed)));
		remainingHealth -= received;
		if(threat.physicalDamage)
			swampRenewalDamage += received;
		if(received <= 0 || !threat.physicalDamage)
			continue;
		const int basisPoints = newHorizonsBulwark::reflectionBasisPoints(bulwarkRank,
			threat.shooting, newHorizonsBulwark::hasThickHide(hero),
			newHorizonsBulwark::hasVengefulMire(hero));
		if(basisPoints <= 0)
			continue;
		const auto * projectedEnemy = defendedPreview->battleGetUnitByID(threat.enemy->unitId());
		if(!projectedEnemy || !projectedEnemy->alive())
			continue;
		const int64_t reflected = newHorizonsBulwark::reflectedDamage(received, basisPoints);
		reflectedDamage += std::min<int64_t>(projectedEnemy->getAvailableHealth(), reflected);
	}

	const float defendValue = damagePrevented + preemptiveValue + reflectedDamage;
	int64_t swampRenewalValue = 0;
	if(newHorizonsBulwark::hasSwampRenewal(hero) && remainingHealth > 0 && swampRenewalDamage > 0)
	{
		const int64_t availableHealing = std::max<int64_t>(0,
			stack->getTotalHealth() - remainingHealth);
		swampRenewalValue = std::min(availableHealing, swampRenewalDamage / 10);
	}
	float sharedCoverDamagePrevented = 0.0f;
	for(const auto & threat : sharedCoverThreats)
		sharedCoverDamagePrevented += std::max(0.0f,
			std::min<float>(threat.ally->getAvailableHealth(), threat.beforeTotal)
			- std::min<float>(threat.ally->getAvailableHealth(), threat.afterTotal));
	const float defendValueWithSharedCover = defendValue + sharedCoverDamagePrevented + swampRenewalValue;
	// Require a visible benefit to beat the existing Wait fallback.  Damage
	// prevented, the one available pre-emptive strike and reflected damage all
	// use the same health-value scale.
	return defendValueWithSharedCover >= std::max<int64_t>(1, stack->getAvailableHealth() / 20);
}
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env),
	cb(cb), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	hb = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	damageCache.buildDamageCache(hb, side);

	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env), cb(cb), hb(hb),
	damageCache(damageCache), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

std::vector<BattleHex> BattleEvaluator::getBrokenWallMoatHexes() const
{
	std::vector<BattleHex> result;

	for(EWallPart wallPart : { EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL })
	{
		auto state = cb->getBattle(battleID)->battleGetWallState(wallPart);

		if(state != EWallState::DESTROYED)
			continue;

		auto wallHex = cb->getBattle(battleID)->wallPartToBattleHex(wallPart);
		auto moatHex = wallHex.cloneInDirection(BattleHex::LEFT);

		result.push_back(moatHex);

		moatHex = moatHex.cloneInDirection(BattleHex::LEFT);
		auto obstaclesSecondRow = cb->getBattle(battleID)->battleGetAllObstaclesOnPos(moatHex, false);

		for(auto obstacle : obstaclesSecondRow)
		{
			if(obstacle->obstacleType == CObstacleInstance::EObstacleType::MOAT)
			{
				result.push_back(moatHex);
				break;
			}
		}
	}

	return result;
}

bool BattleEvaluator::hasWorkingTowers() const
{
	bool keepIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::DESTROYED;
	bool upperIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::DESTROYED;
	bool bottomIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::DESTROYED;
	return keepIntact || upperIntact || bottomIntact;
}

std::optional<PossibleSpellcast> BattleEvaluator::findBestCreatureSpell(const CStack * stack)
{
	if(!stack->canCast())
		return std::nullopt;

	std::vector<SpellID> spellsToCast;
	TConstBonusListPtr bl = stack->getBonusesOfType(BonusType::SPELLCASTER);

	//TODO: faerie dragon type spell should be selected by server
	SpellID creatureSpellToCast = cb->getBattle(battleID)->getRandomCastedSpell(CRandomGenerator::getDefault(), stack);

	for(const auto & bonus : *bl)
		if(!bonus->parameters && bonus->subtype.as<SpellID>().hasValue())
			spellsToCast.push_back(bonus->subtype.as<SpellID>());

	if(creatureSpellToCast.hasValue())
		spellsToCast.push_back(creatureSpellToCast);

	std::vector<PossibleSpellcast> possibleCasts;

	for(const auto spellID : spellsToCast)
	{
		const CSpell * spell = spellID.toSpell();

		if(!spell->canBeCast(cb->getBattle(battleID).get(), spells::Mode::CREATURE_ACTIVE, stack))
			continue;

		spells::BattleCast temp(cb->getBattle(battleID).get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
		{
			PossibleSpellcast ps;
			ps.dest = target;
			ps.spell = spell;
			evaluateCreatureSpellcast(stack, ps);
			possibleCasts.push_back(ps);
		}
	}

	std::sort(
		possibleCasts.begin(), possibleCasts.end(),
		[&](const PossibleSpellcast & lhs, const PossibleSpellcast & rhs)
		{
			return lhs.value > rhs.value;
		}
	);

	if(!possibleCasts.empty() && possibleCasts.front().value > 0)
		return possibleCasts.front();

	return std::nullopt;
}

BattleAction BattleEvaluator::selectStackAction(const CStack * stack)
{
#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace("Select stack action");
#endif
	//evaluate casting spell for spellcasting stack
	std::optional<PossibleSpellcast> bestSpellcast = findBestCreatureSpell(stack);

	auto moveTarget = scoreEvaluator.findMoveTowardsUnreachable(stack, *targets, damageCache, hb);
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});
	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& !stack->canShoot()
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if(targets->possibleAttacks.empty() && bestSpellcast.has_value())
	{
		activeActionMade = true;
		return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
	}

	if(!targets->possibleAttacks.empty())
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating attack for %s", stack->getDescription());
#endif

		auto evaluationResult = scoreEvaluator.findBestTarget(stack, *targets, damageCache, hb, siegeDefense);
		auto & bestAttack = evaluationResult.bestAttack;

		cachedAttack.ap = bestAttack;
		cachedAttack.score = evaluationResult.score;
		cachedAttack.turn = 0;
		cachedAttack.waited = evaluationResult.wait;

		//TODO: consider more complex spellcast evaluation, f.e. because "re-retaliation" during enemy move in same turn for melee attack etc.
		if(bestSpellcast.has_value() && bestSpellcast->value > bestAttack.damageDiff())
		{
			// return because spellcast value is damage dealt and score is dps reduce
			activeActionMade = true;
			return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
		}

		if(evaluationResult.score > score)
		{
			score = evaluationResult.score;

			logAi->debug("BattleAI: %s -> %s x %d, from %d curpos %d dist %d speed %d: +%2f -%2f = %2f",
				bestAttack.attackerState->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->getCount(),
				bestAttack.from.toInt(),
				bestAttack.attack.attacker->getPosition().toInt(),
				bestAttack.attack.chargeDistance,
				bestAttack.attack.attacker->getMovementRange(0),
				bestAttack.defenderDamageReduce,
				bestAttack.attackerDamageReduce,
				score
			);

			if (moveTarget.score <= score)
			{
				if(evaluationResult.wait && !stack->acquireState()->waitedThisTurn)
				{
					return BattleAction::makeWait(stack);
				}
				else if(bestAttack.attack.shooting)
				{
					activeActionMade = true;
					const auto * hero = hb->battleGetFightingHero(stack->unitSide());
					if(bestAttack.from.isValid() && bestAttack.from != stack->getPosition()
						&& newHorizonsArchery::canUseSkirmisher(hero, stack)
						&& hb->battleCanSkirmisherAttackFromHex(stack,
							bestAttack.attack.defender->getPosition(), bestAttack.from))
					{
						// Skirmisher encodes its chosen legal move-and-fire destination through
						// the existing WALK_AND_ATTACK packet.
						auto action = BattleAction::makeMeleeAttack(stack,
							bestAttack.attack.defender->getPosition(), bestAttack.from, false);
						action.archerySkirmisherAttack = true;
						action.perfectMoment = bestAttack.perfectMoment;
						return action;
					}
					auto action = BattleAction::makeShotAttack(stack, bestAttack.attack.defender);
					action.perfectMoment = bestAttack.perfectMoment;
					return action;
				}
				else
				{
					if(bestAttack.collateralDamageReduce
						&& bestAttack.collateralDamageReduce >= bestAttack.defenderDamageReduce / 2
						&& score < 0)
					{
						return BattleAction::makeDefend(stack);
					}

					bool isTargetOutsideFort = !hb->battleIsInsideWalls(bestAttack.from);
					bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
						&& !bestAttack.attack.shooting
						&& hasWorkingTowers()
						&& !enemyMellee.empty()
						&& isTargetOutsideFort;

					if(siegeDefense)
					{
						logAi->trace("Evaluating exchange at %d self-defense", stack->getPosition());

						BattleAttackInfo bai(stack, stack, 0, false);
						AttackPossibility apDefend(stack->getPosition(), stack->getPosition(), bai);

						float defenseValue = scoreEvaluator.evaluateExchange(apDefend, 0, *targets, damageCache, hb);

						if((defenseValue > score && score <= 0) || (defenseValue > 2 * score && score > 0))
						{
							return BattleAction::makeDefend(stack);
						}
					}
					
					activeActionMade = true;
					auto action = BattleAction::makeMeleeAttack(stack, bestAttack.attack.defenderPos, bestAttack.from);
					action.perfectMoment = bestAttack.perfectMoment;
					return action;
				}
			}
		}
	}

	//ThreatMap threatsToUs(stack); // These lines may be useful but they are't used in the code.
	if(moveTarget.score > score)
	{
		score = moveTarget.score;
		cachedAttack.ap = moveTarget.cachedAttack;
		cachedAttack.score = score;
		cachedAttack.turn = moveTarget.turnsToReach;

		if(stack->acquireState()->waitedThisTurn)
		{
			logAi->debug(
				"Moving %s towards hex %s[%d], score: %2f",
				stack->getDescription(),
				moveTarget.cachedAttack->attack.defender->getDescription(),
				moveTarget.cachedAttack->attack.defender->getPosition(),
				moveTarget.score);

			return goTowardsNearest(stack, moveTarget.positions, *targets);
		}
		else
		{
			cachedAttack.waited = true;
			return BattleAction::makeWait(stack);
		}
	}

	if(score <= EvaluationResult::INEFFECTIVE_SCORE
		&& !stack->hasBonusOfType(BonusType::FLYING)
		&& stack->getMovementRange(0) != 0
		&& stack->unitSide() == BattleSide::ATTACKER
	   && cb->getBattle(battleID)->battleGetFortifications().hasMoat)
	{
		auto brokenWallMoat = getBrokenWallMoatHexes();

		if(brokenWallMoat.size())
		{
			activeActionMade = true;

			if(stack->doubleWide() && vstd::contains(brokenWallMoat, stack->getPosition()))
				return BattleAction::makeMove(stack, stack->getPosition().cloneInDirection(BattleHex::RIGHT));
			else
				return goTowardsNearest(stack, brokenWallMoat, *targets);
		}
	}

	if(stack->acquireState()->waitedThisTurn)
		return BattleAction::makeDefend(stack);

	if(defensiveStanceMakesDefendWorthwhile(env.get(), hb, stack))
		return BattleAction::makeDefend(stack);
	return BattleAction::makeWait(stack);
}

uint64_t timeElapsed(std::chrono::time_point<std::chrono::steady_clock> start)
{
	auto end = std::chrono::steady_clock::now();

	return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

BattleAction BattleEvaluator::moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets)
{
	auto additionalScore = 0;
	std::optional<AttackPossibility> attackOnTheWay;

	for(auto & target : targets.possibleAttacks)
	{
		if(!target.attack.shooting && target.from == hex && target.attackValue() > additionalScore)
		{
			additionalScore = target.attackValue();
			attackOnTheWay = target;
		}
	}

	if(attackOnTheWay)
	{
		activeActionMade = true;
		auto action = BattleAction::makeMeleeAttack(stack, attackOnTheWay->attack.defender->getPosition(), attackOnTheWay->from);
		action.perfectMoment = attackOnTheWay->perfectMoment;
		return action;
	}
	else
	{
		if(stack->position == hex)
			return BattleAction::makeDefend(stack);
		else
			return BattleAction::makeMove(stack, hex);
	}
}

BattleAction BattleEvaluator::goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets)
{
	auto reachability = cb->getBattle(battleID)->getReachability(stack);
	auto avHexes = cb->getBattle(battleID)->battleGetAvailableHexes(reachability, stack, false);

	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});

	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if (siegeDefense)
	{
		avHexes.eraseIf([&](const BattleHex & hex)
		{
			return !cb->getBattle(battleID)->battleIsInsideWalls(hex);
		});
	}

	if(avHexes.empty() || hexes.empty()) //we are blocked or dest is blocked
	{
		return BattleAction::makeDefend(stack);
	}

	BattleHexArray targetHexes = hexes;

	targetHexes.sort([&reachability](const BattleHex & h1, const BattleHex & h2) -> bool
		{
			return reachability.distances[h1.toInt()] < reachability.distances[h2.toInt()];
		});

	BattleHex bestNeighbour = targetHexes.front();

	if(reachability.distances[bestNeighbour.toInt()] > GameConstants::BFIELD_SIZE)
	{
		logAi->trace("No reachable hexes.");
		return BattleAction::makeDefend(stack);
	}

	// this turn
	for(const auto & hex : targetHexes)
	{
		if(avHexes.contains(hex))
		{
			return moveOrAttack(stack, hex, targets);
		}

		if(stack->coversPos(hex))
		{
			logAi->warn("Warning: already standing on neighbouring hex!");
			//We shouldn't even be here...
			return BattleAction::makeDefend(stack);
		}
	}

	// not this turn
	scoreEvaluator.updateReachabilityMap(hb);

	if(stack->hasBonusOfType(BonusType::FLYING))
	{
		BattleHexArray obstacleHexes;

		const auto & obstacles = hb->battleGetAllObstacles();

		for (const auto & obst : obstacles) 
		{
			if(obst->triggersEffects())
			{
				auto triggerAbility =  LIBRARY->spells()->getById(obst->getTrigger());
				auto triggerIsNegative = triggerAbility->isNegative() || triggerAbility->isDamage();

				if(triggerIsNegative)
					obstacleHexes.insert(obst->getAffectedTiles());
			}
		}
		// Flying stack doesn't go hex by hex, so we can't backtrack using predecessors.
		// We just check all available hexes and pick the one closest to the target.
		auto nearestAvailableHex = vstd::minElementByFun(avHexes, [this, &bestNeighbour, &stack, &obstacleHexes](const BattleHex & hex) -> int
		{
			const int NEGATIVE_OBSTACLE_PENALTY = 100; // avoid landing on negative obstacle (moat, fire wall, etc)
			const int BLOCKED_STACK_PENALTY = 100; // avoid landing on moat

			auto distance = BattleHex::getDistance(bestNeighbour, hex);

			if(obstacleHexes.contains(hex))
				distance += NEGATIVE_OBSTACLE_PENALTY;

			return scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, hex) ? BLOCKED_STACK_PENALTY + distance : distance;
		});

		return moveOrAttack(stack, *nearestAvailableHex, targets);
	}
	else
	{
		BattleHex currentDest = bestNeighbour;

		while(true)
		{
			if(!currentDest.isValid())
			{
				return BattleAction::makeDefend(stack);
			}

			if(avHexes.contains(currentDest)
				&& !scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, currentDest))
			{
				return moveOrAttack(stack, currentDest, targets);
			}

			currentDest = reachability.predecessors[currentDest.toInt()];
		}
	}
	
	logAi->error("We should either detect that hexes are unreachable or make a move!");
	return BattleAction::makeDefend(stack);
}

bool BattleEvaluator::canCastSpell()
{
	auto hero = cb->getBattle(battleID)->battleGetMyHero();
	if(!hero)
		return false;

	if(cb->getBattle(battleID)->battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK)
		return true;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		if(cb->getBattle(battleID)->battleCanUseHeroCommand(side, command))
			return true;
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
		if(cb->getBattle(battleID)->battleCanBeginHeroCommand(side, command))
			return true;
	return false;
}

bool BattleEvaluator::attemptCastingSpell(const CStack * activeStack, bool allowSpells)
{
	auto hero = cb->getBattle(battleID)->battleGetMyHero();
	if(!hero)
		return false;

	LOGL("Casting spells sounds like fun. Let's see...");
	const bool metamagicFollowup = cb->getBattle(battleID)->battleCanUseMetamagicFollowup(side);
	const bool activatesGrand = HeroSpellAllowanceTransition::activatesGrand(metamagicFollowup,
		cb->getBattle(battleID)->battleMetamagicPendingCount(side),
		cb->getBattle(battleID)->battleMetamagicSequenceSpells(side).size(),
		cb->getBattle(battleID)->battleMetamagicUsesConsumed(side),
		newHorizonsMagic::metamagicRank(hero),
		newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
		cb->getBattle(battleID)->battleMetamagicGrandUsed(side));
	// Grand is an automatic outcome of the third used sequence, never a
	// selectable alternative. Project exactly the transition the server applies.
	//Get all spells we can cast
	struct SpellOption
	{
		const CSpell * spell = nullptr;
		bool metamagicGrand = false;
	};
	std::vector<SpellOption> possibleSpells;

	for(auto const & s : LIBRARY->spellh->objects)
		if(allowSpells && s->canBeCast(cb->getBattle(battleID).get(), spells::Mode::HERO, hero, activatesGrand))
			possibleSpells.push_back({s.get(), activatesGrand});

	LOGFL("I can cast %d spells.", possibleSpells.size());

	vstd::erase_if(possibleSpells, [](const SpellOption & option)
	{
		return spellType(option.spell) != SpellTypes::BATTLE && !isCounterspell(option.spell);
	});

	LOGFL("I know how %d of them works.", possibleSpells.size());

	const auto battleCallback = cb->getBattle(battleID);
	const bool hasHolyArmor = std::any_of(possibleSpells.begin(), possibleSpells.end(), [](const SpellOption & option)
	{
		return isCanonicalHolyArmor(option.spell);
	});
	std::vector<const battle::Unit *> visibleEnemyUnits;
	int64_t friendlyAvailableHealth = 0;
	float visibleMagicalSpellThreat = 0.0f;
	if(hasHolyArmor)
	{
		for(const auto * unit : battleCallback->battleGetAllUnits(false))
		{
			if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
				continue;

			if(unit->unitSide() == side)
				friendlyAvailableHealth += std::max<int64_t>(0, unit->getAvailableHealth());
			else
				visibleEnemyUnits.push_back(unit);
		}
		visibleMagicalSpellThreat = visibleCreatureSpellThreat(visibleEnemyUnits, true);
	}

	//Get viable spell-target pairs
	std::vector<PossibleSpellcast> possibleCasts;
	for(const auto & spellOption : possibleSpells)
	{
		const auto * spell = spellOption.spell;
		const bool metamagicGrandChoice = spellOption.metamagicGrand;
		if(isCounterspell(spell))
		{
			// Counterspell has a no-target cast and no ordinary spell effect to
			// project.  Give it a dedicated threat score instead of allowing the
			// generic effect evaluator to treat the ward as a zero-value cast.
			const auto value = counterspellThreatValue(*cb->getBattle(battleID), side, hero, spell);
			if(value <= 0.0f)
				continue;

			PossibleSpellcast ps;
			ps.spell = spell;
			ps.dest = {spells::Destination()};
			ps.metamagicFollowup = metamagicFollowup;
			ps.metamagicGrand = metamagicGrandChoice;
			ps.value = value;
			possibleCasts.push_back(std::move(ps));
			continue;
		}

		const int maxOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
			cb->getBattle(battleID)->getBattle()->getMagicRules(), spell->getId(), hero->getEffectPower(spell),
			newHorizonsMagic::magicArrowOverchargeModifiers(hero));
		const bool canUseSelectiveDispel = spell->getId() == SpellID::DISPEL
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel");
		const bool canUseTemporalField = spell->getId() == SpellID::SLOW
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField")
			&& !cb->getBattle(battleID)->battleWasTemporalFieldUsed(side);
		std::vector<SpellID> cureAfflictionChoices{SpellID::NONE};
		const auto & magicRules = cb->getBattle(battleID)->getBattle()->getMagicRules();
		if(newHorizonsMagic::cureEnabled(magicRules, spell->getId()))
		{
			for(const auto * unit : cb->getBattle(battleID)->battleGetAllUnits(false))
				for(const auto affliction : newHorizonsMagic::cureAfflictions(magicRules, unit))
					if(!vstd::contains(cureAfflictionChoices, affliction))
						cureAfflictionChoices.push_back(affliction);
			std::sort(cureAfflictionChoices.begin(), cureAfflictionChoices.end(), [](const SpellID & lhs, const SpellID & rhs)
			{
				return lhs.getNum() < rhs.getNum();
			});
		}

		for(const auto cureAffliction : cureAfflictionChoices)
		for(const bool massSlow : {false, true})
		{
			if(massSlow && !canUseTemporalField)
				continue;
			for(const bool selectiveDispel : {false, true})
			{
				if(selectiveDispel && !canUseSelectiveDispel)
					continue;
				spells::BattleCast temp(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
				temp.setMetamagicFollowup(metamagicFollowup);
				temp.setMetamagicGrand(metamagicGrandChoice);
				temp.setCureAffliction(cureAffliction);
				temp.setMassSlow(massSlow);
				temp.setSelectiveDispel(selectiveDispel);
				for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
				{
					for(int overcharge = 0; overcharge <= maxOvercharge; ++overcharge)
					{
						spells::BattleCast candidateCast(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
						candidateCast.setMetamagicFollowup(metamagicFollowup);
						candidateCast.setMetamagicGrand(metamagicGrandChoice);
						candidateCast.setCureAffliction(cureAffliction);
						if(!target.empty() && target.front().unitValue)
							candidateCast.setMetamagicTargetUnitId(target.front().unitValue->unitId());
						candidateCast.setOvercharge(overcharge);
						candidateCast.setMassSlow(massSlow);
						candidateCast.setSelectiveDispel(selectiveDispel);
						auto candidateMechanics = spell->battleMechanics(&candidateCast);
						spells::detail::ProblemImpl problem;
						const bool stormOfDaggers = candidateMechanics->isNewHorizonsStormOfDaggers();
						if(stormOfDaggers
							&& (!candidateMechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(target.size()))
								|| !candidateMechanics->canBeCastAt(target, problem)))
							continue;
						if(!candidateMechanics->canBeCast(problem))
							continue;

						PossibleSpellcast ps;
						ps.dest = target;
						// NO_LOCATION is represented on the wire by one invalid
						// destination.  Keep a concrete sentinel in the hypothetical
						// cast too: BattleSpellMechanics::castEval intentionally rejects
						// an entirely empty aim, while mass effects use the invalid
						// destination to collect every eligible unit.
						if(massSlow && ps.dest.empty())
							ps.dest.emplace_back(BattleHex::INVALID);
						ps.spell = spell;
						ps.metamagicFollowup = metamagicFollowup;
						ps.metamagicGrand = metamagicGrandChoice;
						ps.spellOvercharge = overcharge;
						ps.spellSelectiveDispel = selectiveDispel;
						ps.spellCureAffliction = cureAffliction;
						ps.spellMassSlow = massSlow;
						ps.spellStormOfDaggers = stormOfDaggers;
						if(isCanonicalLandMine(*cb->getBattle(battleID), spell))
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::landMinePlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
						if(isSelectedQuicksand(*cb->getBattle(battleID), spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::quicksandPlacementValue(
								candidateMechanics.get(), ps.dest);
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalFireWall(*cb->getBattle(battleID), spell))
						{
							ps.spellFireWallDirection = fireWallDirection(ps.dest);
							if(ps.spellFireWallDirection == BattleHex::NONE)
								continue;
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::fireWallPlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							// A zero-value line is either exposed to friendly ground or has no
							// reachable hostile pressure. Do not let the generic hypothetical
							// cast evaluator turn such a delayed placement into an accidental
							// positive action.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalTimeStop(spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::timeStopPlacementValue(
								candidateMechanics.get(), ps.dest);
							// Time Stop is neutral by content definition, so the generic
							// target comparer cannot tell a helpful enemy footprint from
							// a harmful healthy-ally footprint.  Require a strictly
							// beneficial placement before exposing it to action ranking.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalSpellLock(spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::spellLockPlacementValue(
								candidateMechanics.get(), ps.dest);
							// Spell Lock is indifferent at the content layer. Do not leave
							// generic evaluation free to value a no-op seal as a cast.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(newHorizonsMagic::physicalPoisonEnabled(
							cb->getBattle(battleID)->getBattle()->getMagicRules(), spell->getId()))
						{
							ps.spellNaturePoisonValue = SpellTargetEvaluator::naturePoisonPlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							if(ps.spellNaturePoisonValue <= 0.0f)
								continue;
						}
						if(spell->getJsonKey() == newHorizonsPlague::SPELL_ID)
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::plagueDelayedDamageValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalHolyArmor(spell))
						{
							if(visibleMagicalSpellThreat <= 0.0f || friendlyAvailableHealth <= 0
								|| ps.dest.size() != 1 || !ps.dest.front().unitValue
								|| ps.dest.front().unitValue->unitSide() != side)
								continue;
						}
						possibleCasts.push_back(ps);
					}
				}
			}
		}
	}
	// Authoritative queries decide whether an Order allowance is available.
	// Possessing a Spell Action must not hide a separate legal Order Action.
	// Side-wide Orders use the authoritative availability query.  Targeted Orders
	// are enumerated through the callback's legal target-set query, with no local
	// guess about action budget, ownership, or current-round state.
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		if(cb->getBattle(battleID)->battleCanUseHeroCommand(side, command))
		{
			PossibleSpellcast candidate;
			candidate.command = command;
			if(heroCommands::isCanonicalRules(cb->getBattle(battleID)->getBattle()->getHeroCommandRules()))
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, {});
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(candidate);
		}
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
	{
		for(const auto & targetIds : orderTargetOptions(*cb->getBattle(battleID), side, command))
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, command, targetIds))
				continue;

			PossibleSpellcast candidate;
			candidate.command = command;
			candidate.commandTargets = targetIds;
			if(command == HeroCommand::FOCUS_FIRE)
			{
				if(targetIds.size() != 1)
					continue;
				candidate.focusFire = cb->getBattle(battleID)->battlePrepareFocusFireState(side, targetIds.front());
				if(!candidate.focusFire)
					continue;
				const auto & recipients = candidate.focusFire->recipientUnitIds;
				const bool hasRemainingShooter = std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
				{
					const auto * unit = cb->getBattle(battleID)->battleGetUnitByID(id);
					return unit && unit->willMove(0) && unit->canShoot();
				});
				if(!hasRemainingShooter)
					continue;
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, targetIds);
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			else
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*cb->getBattle(battleID), side, command, targetIds);
				if(candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(std::move(candidate));
		}
	}
	LOGFL("Found %d spell-target combinations.", possibleCasts.size());
	if(possibleCasts.empty())
	{
		return false;
	}

	using ValueMap = PossibleSpellcast::ValueMap;

	auto evaluateQueue = [&](ValueMap & values, const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state, size_t minTurnSpan, bool * enemyHadTurnOut) -> bool
	{
		bool firstRound = true;
		bool enemyHadTurn = false;
		size_t ourTurnSpan = 0;

		bool stop = false;

		for(auto & round : queue)
		{
			if(!firstRound)
				state->nextRound();
			for(auto queuedUnit : round)
			{
				const auto * unit = state->battleGetUnitByID(queuedUnit->unitId());
				if(!unit)
					continue;
				if(!vstd::contains(values, unit->unitId()))
					values[unit->unitId()] = 0;

				if(!unit->alive())
					continue;

				if(state->battleGetOwner(unit) != playerID)
				{
					enemyHadTurn = true;

					const auto enemySide = state->playerToSide(state->battleGetOwner(unit));
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

				state->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);

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

				auto bav = potentialTargets.bestActionValue();

				//best action is from effective owner`s point if view, we need to convert to our point if view
				if(state->battleGetOwner(unit) != playerID)
					bav = -bav;
				values[unit->unitId()] += bav;
				state->getForUpdate(unit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			}

			firstRound = false;

			if(stop)
				break;
		}

		if(enemyHadTurnOut)
			*enemyHadTurnOut = enemyHadTurn;

		return ourTurnSpan >= minTurnSpan;
	};

	ValueMap valueOfStack;
	ValueMap healthOfStack;

	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	size_t ourRemainingTurns = 0;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
		valueOfStack[unit->unitId()] = 0;

		if(cb->getBattle(battleID)->battleGetOwner(unit) == playerID && unit->canMove() && !unit->moved())
			ourRemainingTurns++;
	}

	LOGFL("I have %d turns left in this round", ourRemainingTurns);

	const bool castNow = ourRemainingTurns <= 1;

	if(castNow)
		print("I should try to cast a spell now");
	else
		print("I could wait better moment to cast a spell");

	auto amount = all.size();

	std::vector<battle::Units> turnOrder;

	cb->getBattle(battleID)->battleGetTurnOrder(turnOrder, amount, 2); //no more than 1 turn after current, each unit at least once
	std::vector<battle::Units> regenerationTurnOrder;
	cb->getBattle(battleID)->battleGetTurnOrder(regenerationTurnOrder, 0, 4);

	{
		bool enemyHadTurn = false;

		auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));

		evaluateQueue(valueOfStack, turnOrder, state, 0, &enemyHadTurn);

		if(!enemyHadTurn)
		{
			auto battleIsFinishedOpt = state->battleIsFinished();

			if(battleIsFinishedOpt && !metamagicFollowup)
			{
				print("No need to cast a spell. Battle will finish soon.");
				return false;
			}
		}
	}

	CStopWatch timer;

#if BATTLE_TRACE_LEVEL >= 1
	tbb::blocked_range<size_t> r(0, possibleCasts.size());
#else
	tbb::parallel_for(tbb::blocked_range<size_t>(0, possibleCasts.size()), [&](const tbb::blocked_range<size_t> & r)
		{
#endif
			for(auto i = r.begin(); i != r.end(); i++)
			{
				auto & ps = possibleCasts[i];
				auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
				const auto baseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
					? cachedAttack.score : 0.0f;
				std::optional<HypotheticBattle::ProjectedSpellAllowance> spellAllowance;
				std::optional<HypotheticBattle::ProjectedOrderAllowance> orderAllowance;
				HypotheticBattle::ProjectedCounterspellOutcome counterspell;
				bool counterspellNegated = false;
				uint32_t targetId = std::numeric_limits<uint32_t>::max();

				if(ps.command == HeroCommand::NONE)
				{
					spellAllowance = state->prepareHeroSpellAllowance(side, ps.metamagicFollowup, ps.metamagicGrand);
					if(!spellAllowance)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					if(!state->beginProjectedHeroAction(side, *spellAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					counterspell = state->resolveProjectedCounterspell(side, ps.spell);
					if(!counterspell.resolutionKnown || !counterspell.negated.has_value())
					{
						// The armed ward is public, but the opposing hero's mana and
						// Countermage perk may be hidden from this player's callback.
						// Do not invent either a successful or failed Counterspell result.
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					counterspellNegated = *counterspell.negated;
				}
				else
				{
					orderAllowance = state->prepareHeroOrderAllowance(side);
					if(!orderAllowance)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					if(!state->beginProjectedHeroAction(side, *orderAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
				}
				if(isCounterspell(ps.spell))
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					continue;
				}
				// Canonical delayed spells such as Land Mine, Fire Wall, and Plague may
				// have no immediate unit-health delta. Keep their deterministic
				// live-snapshot value instead of allowing the generic hypothetical cast
				// path to collapse a legal delayed effect to zero before it triggers.
				if(ps.command == HeroCommand::NONE && ps.spellPlacementHeuristicValue > 0.0f)
				{
					// A delayed spell still consumes the hero exchange; preserve the
					// same best-attack baseline used by contextual Orders so its
					// placement heuristic is compared on the shared BattleAI scale.
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellPlacementHeuristicValue;
					continue;
				}
				// Canonical Nature Poison changes only the target's physical
				// Poison state. Its detached, three-activation marginal value was
				// forecast during target enumeration, so avoid a second generic
				// effect projection that has no immediate health delta.
				if(ps.command == HeroCommand::NONE && ps.spellNaturePoisonValue > 0.0f)
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellNaturePoisonValue;
					continue;
				}
				// Contextual Orders have no faithful projection in the old
				// hypothetical-battle model: targeted Orders and trigger/relationship
				// state carry more information than ordinary unit bonuses.  Their
				// deterministic read-only value was computed while enumerating the
				// authoritative legal target set.  Keep that score intact so they
				// still compete with spells and normal attacks.
				if(ps.command != HeroCommand::NONE && ps.commandHeuristicValue > 0.0f)
				{
					// An Order consumes the hero's exchange but does not replace the
					// unit action that follows it.  Keep the normal best-action score
					// in the candidate value so contextual Orders compete on the same
					// scale as spells and ordinary attacks rather than being treated as
					// a small, standalone bonus.
					if(!state->projectAcceptedHeroOrder(side, *orderAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.commandHeuristicValue;
					continue;
				}

#if BATTLE_TRACE_LEVEL >= 1
				if(ps.dest.empty())
					logAi->trace("Evaluating %s", ps.name());
				else
				{
					auto psFirst = ps.dest.front();
					auto strWhere = psFirst.unitValue ? psFirst.unitValue->getDescription() : std::to_string(psFirst.hexValue.toInt());

					logAi->trace("Evaluating %s at %s", ps.name(), strWhere);
				}
#endif

				if(ps.command == HeroCommand::NONE)
				{
					auto candidateTarget = ps.dest;
					bool missingProjectedTarget = false;
					if(!candidateTarget.empty() && candidateTarget.front().unitValue)
						targetId = candidateTarget.front().unitValue->unitId();
					for(auto & destination : candidateTarget)
					{
						if(!destination.unitValue)
							continue;
						const auto destinationId = destination.unitValue->unitId();
						destination.unitValue = state->battleGetUnitByID(destinationId);
						missingProjectedTarget |= destination.unitValue == nullptr;
					}
					if(missingProjectedTarget)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, ps.spell);
					cast.setMetamagicFollowup(ps.metamagicFollowup);
					cast.setMetamagicGrand(ps.metamagicGrand);
					if(targetId != std::numeric_limits<uint32_t>::max())
						cast.setMetamagicTargetUnitId(targetId);
					cast.setOvercharge(ps.spellOvercharge);
					cast.setSelectiveDispel(ps.spellSelectiveDispel);
					cast.setCureAffliction(ps.spellCureAffliction);
					cast.setMassSlow(ps.spellMassSlow);
					if(counterspell.wardActive)
						cast.setCounterspell(counterspell.wardSide, counterspellNegated);
					if(!counterspellNegated)
					{
						auto mechanics = ps.spell->battleMechanics(&cast);
						if(mechanics->isNewHorizonsStormOfDaggers())
						{
							// The selected subset size is part of this cast's shared damage
							// context. Use the ordinary mechanics forecast after setting it so
							// school scaling and target resistance follow the authoritative path.
							if(!mechanics->setStormOfDaggersTargetCount(
								static_cast<int32_t>(candidateTarget.size())))
							{
								ps.value = std::numeric_limits<float>::lowest();
								continue;
							}
							mechanics->castEval(state->getServerCallback(), candidateTarget);
						}
						else
						{
							mechanics->castEval(state->getServerCallback(), candidateTarget);
							// Authoritative BattleSpellMechanics::cast captures this fixed-point
							// snapshot after applying the timed marker. castEval deliberately
							// projects effect packets only, so mirror that snapshot onto the
							// cloned accepted target for future-wound valuation.
							projectRegenerationRateSnapshot(*state, *mechanics, ps.spell, candidateTarget);
						}
					}
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
				}
				else if(ps.command == HeroCommand::FOCUS_FIRE)
				{
					state->setFocusFireState(side, ps.focusFire.value());
				}
				else
				{
					const auto effects = heroCommands::bonuses(state->getHeroCommandRules(), ps.command, *hero);
					for(const auto * unit : state->battleGetAllStacks(true))
					{
						if(state->battleGetOwner(unit) != playerID)
							continue;
						if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
							state->addUnitBonus(unit->unitId(), effects);
					}
				}

				if(ps.command != HeroCommand::NONE && !state->projectAcceptedHeroOrder(side, *orderAllowance))
				{
					ps.value = std::numeric_limits<float>::lowest();
					continue;
				}

				// Removed sacrifice victims must remain in the health accounting below.
				auto allUnits = state->battleGetUnitsIf([](const battle::Unit * u) -> bool { return !u->isTurret(); });
				const bool transfigureMatter = isTransfigureMatter(ps.spell);
				const bool phantomArmy = isPhantomArmy(ps.spell);

				auto needFullEval = ps.command == HeroCommand::FOCUS_FIRE
					|| state->hasObstacleChanges() || state->hasWallChanges()
					|| vstd::contains_if(allUnits, [&](const battle::Unit * u) -> bool
					{
						auto original = cb->getBattle(battleID)->battleGetUnitByID(u->unitId());
						return !original || u->getMovementRange() != original->getMovementRange()
							|| (ps.spell && ps.spell->getId() == SpellID::SLOW
								&& u->getInitiative() != original->getInitiative())
							|| u->getPosition() != original->getPosition()
							|| u->alive() != original->alive() || u->isGhost() != original->isGhost();
					});

				DamageCache safeCopy = damageCache;
				DamageCache innerCache(&safeCopy);

				innerCache.buildDamageCache(state, side);

				if(cachedAttack.ap && cachedAttack.waited)
				{
					state->makeWait(activeStack);
				}

				float stackActionScore = 0;
				float damageToHostilesScore = 0;
				float damageToFriendliesScore = 0;
				float initiativeEffectScore = 0;
				float projectedDebuffScore = 0;
				if(isCanonicalRegeneration(ps.spell))
				{
					const auto targets = improvedRegenerationTargets(
						*state, *cb->getBattle(battleID), ps.spell->getId());
					const auto regenerationValue = projectedRegenerationValue(env.get(), cb->getBattle(battleID), state,
						regenerationTurnOrder, targets, innerCache, side, playerID,
						scoreEvaluator.getPositiveEffectMultiplier());
					damageToHostilesScore += regenerationValue;
				}
				if(isCanonicalHolyArmor(ps.spell) && targetId != std::numeric_limits<uint32_t>::max())
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto mitigationValue = holyArmorMitigationValue(side, liveTarget, projectedTarget,
						ps.spell->getId(), friendlyAvailableHealth, visibleMagicalSpellThreat, innerCache, state);
					damageToHostilesScore += mitigationValue * scoreEvaluator.getPositiveEffectMultiplier();
				}

				const auto modelActive = state->getForUpdate(activeStack->unitId());
				if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
				{
#if BATTLE_TRACE_LEVEL >= 1
					logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

					PotentialTargets innerTargets(modelActive.get(), innerCache, state);
					BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

					innerEvaluator.updateReachabilityMap(state);

					auto moveTarget = innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state);

					if(!innerTargets.possibleAttacks.empty())
					{
						auto newStackAction = innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state);

						stackActionScore = std::max(moveTarget.score, newStackAction.score);
					}
					else
					{
						stackActionScore = moveTarget.score;
					}
				}
				else if(modelActive->alive())
				{
					auto updatedAttacker = state->getForUpdate(cachedAttack.ap->attack.attacker->unitId());
					auto updatedDefender = state->getForUpdate(cachedAttack.ap->attack.defender->unitId());
					auto updatedBai = BattleAttackInfo(
						updatedAttacker.get(),
						updatedDefender.get(),
						cachedAttack.ap->attack.chargeDistance,
						cachedAttack.ap->attack.shooting);

					auto updatedAttack = AttackPossibility::evaluate(updatedBai, cachedAttack.ap->from, innerCache, state, cachedAttack.ap->perfectMoment);

					BattleExchangeEvaluator innerEvaluator(scoreEvaluator);

					stackActionScore = innerEvaluator.evaluateExchange(updatedAttack, cachedAttack.turn, *targets, innerCache, state);
				}
				for(const auto & unit : allUnits)
				{
					if(!unit->isValidTarget(true) && !vstd::contains(healthOfStack, unit->unitId()))
						continue;

					auto newHealth = unit->getAvailableHealth();
					auto oldHealth = vstd::find_or(healthOfStack, unit->unitId(), 0); // old health value may not exist for newly summoned units
					auto original = cb->getBattle(battleID)->battleGetUnitByID(unit->unitId());
					if(ps.spell && original && state->battleGetOwner(unit) != playerID)
					{
						if(ps.spell->getId() == SpellID::SORROW)
							projectedDebuffScore += estimateProjectedSorrowTargetValue(original, unit, innerCache, state);
						else if(ps.spell->getId() == SpellID::CURSE)
							projectedDebuffScore += estimateProjectedCurseTargetValue(original, unit, innerCache, state);
						else if(isCanonicalHexOfPain(ps.spell))
							projectedDebuffScore += estimateProjectedHexOfPainTargetValue(original, unit, state);
						else if(isCanonicalFrailty(ps.spell) && unit->unitId() == targetId)
							projectedDebuffScore += estimateProjectedFrailtyTargetValue(original, unit, innerCache, state);
					}
					const bool phantomArmyStack = phantomArmy && !original
						&& state->battleGetOwner(unit) == playerID
						&& phantomArmyInitialIntegrity(unit, ps.spell->getId()) > 0;
					if(phantomArmyStack)
					{
						// Summoned stacks are excluded from the ordinary health-delta score
						// below.  Score this copied army once from its full count, its
						// dedicated integrity pool, and the spell's two-round duration.
						damageToHostilesScore += phantomArmyCombatValue(unit, ps.spell->getId());
					}
					if(transfigureMatter && !original && unit->unitType()
						&& unit->unitType()->getJsonKey() == "core:diamondGolem"
						&& state->battleGetOwner(unit) == playerID && newHealth > 0)
					{
						// A Transfigure Matter cast creates temporary Diamond Golems. Use
						// their projected partial-stack health and normal creature AI
						// value, rather than dropping all magical summons from scoring.
						const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
						damageToHostilesScore += static_cast<float>(newHealth)
							* static_cast<float>(unit->unitType()->getAIValue())
							/ static_cast<float>(maxHealth);
					}
					if(ps.spell && ps.spell->getId() == SpellID::SLOW
						&& original && original->alive() && unit->alive())
					{
						const int oldInitiative = std::max(1, original->getInitiative());
						const int initiativeDelta = unit->getInitiative() - original->getInitiative();
						const int signedDelta = state->battleGetOwner(unit) == playerID
							? initiativeDelta : -initiativeDelta;
						const float stackValue = static_cast<float>(unit->getCount()) * unit->unitType()->getAIValue();
						initiativeEffectScore += stackValue * static_cast<float>(signedDelta)
							/ static_cast<float>(oldInitiative) * 0.01f;
					}

					if(oldHealth != newHealth)
					{
						auto damage = std::abs(oldHealth - newHealth);
						auto originalDefender = original;

						auto dpsReduce = AttackPossibility::calculateDamageReduce(
							nullptr,
							originalDefender && originalDefender->alive() ? originalDefender : unit,
							damage,
							innerCache,
							state);
						const bool ourUnit = state->battleGetOwner(unit) == playerID;
						const bool goodEffect = newHealth > oldHealth;
						if(ps.spellStormOfDaggers && !ourUnit && original)
						{
							// The authoritative cast independently resists each hostile
							// target. castEval uses the shared ranked damage amount but leaves
							// that random roll out, so value its expected hit probability here.
							const int resistance = std::clamp(original->magicResistance(), 0, 100);
							dpsReduce *= 1.0f - static_cast<float>(resistance) / 100.0f;
						}

						if(ourUnit == goodEffect)
						{
							auto isMagical = state->getForUpdate(unit->unitId())->summoned
								|| unit->isClone()
								|| unit->isGhost()
								|| phantomArmyStack;

							if(ourUnit && goodEffect && isMagical)
								continue;

							damageToHostilesScore += dpsReduce * scoreEvaluator.getPositiveEffectMultiplier();
						}
						else
							// discourage AI making collateral damage with spells
							damageToFriendliesScore -= 4 * dpsReduce * scoreEvaluator.getNegativeEffectMultiplier();

#if BATTLE_TRACE_LEVEL >= 1
						// Ensure ps.dest is not empty before accessing the first element
						if (!ps.dest.empty()) 
						{
							logAi->trace(
								"Spell %s to %d affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								ps.dest.at(0).hexValue.toInt(),  // Safe to access .at(0) now
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
						else 
						{
							// Handle the case where ps.dest is empty
							logAi->trace(
								"Spell %s has no destination, affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
#endif
					}
				}
				damageToHostilesScore += projectedDebuffScore * scoreEvaluator.getPositiveEffectMultiplier();

				if (vstd::isAlmostEqual(stackActionScore, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)))
				{
					ps.value = damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}
				else
				{
				ps.value = stackActionScore + damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}
#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("Total score for %s: %2f (action: %2f, friedly damage: %2f, hostile damage: %2f)", ps.name(), ps.value, stackActionScore, damageToFriendliesScore, damageToHostilesScore);
#endif
			}
#if BATTLE_TRACE_LEVEL == 0
		});
#endif

	// No effective ordinary action still permits declining a harmful spell.
	// Use this same baseline for casts and their projected continuations.
	const auto noCastBaseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
		? cachedAttack.score : 0.0f;

	// Re-evaluate a granted continuation against the actual post-cast battlefield
	// on the next decision. Do not add a second copy of a pre-cast damage score:
	// the first spell may remove its target or spend the mana the second needs.

	LOGFL("Evaluation took %d ms", timer.getDiff());

	auto castToPerform = *vstd::maxElementByFun(possibleCasts, [](const PossibleSpellcast & ps) -> float
		{
			return ps.value;
		});
	if(metamagicFollowup
		&& (castToPerform.value < noCastBaseline
			|| vstd::isAlmostEqual(castToPerform.value, noCastBaseline)))
	{
		// Keeping the allowance is the no-cast baseline, not literal score zero: hypothetical
		// spell values include the active exchange's projected attack. A legal
		// follow-up can therefore be positive in absolute terms while still
		// being harmful relative to leaving the active exchange untouched.
		LOGL("No beneficial hero action; retaining the Spell Action for this round.");
		return false;
	}

	if(metamagicFollowup || (castToPerform.value > noCastBaseline && !vstd::isAlmostEqual(castToPerform.value, noCastBaseline)))
	{
		LOGFL("Best hero action is %s (value %d). Will perform.", castToPerform.name() % castToPerform.value);
		if(castToPerform.command != HeroCommand::NONE)
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, castToPerform.command,
				castToPerform.commandTargets))
				return false;

			if(castToPerform.command == HeroCommand::FOCUS_FIRE)
			{
				const auto targetId = castToPerform.focusFire.value().targetUnitId;
				if(!cb->getBattle(battleID)->battleCanConfirmHeroCommand(side, castToPerform.command, targetId))
					return false;
				cb->battleMakeSpellAction(battleID,
					BattleAction::makeTargetedHeroCommand(side, castToPerform.command, targetId));
			}
			else
			{
				BattleAction action;
				if(castToPerform.commandTargets.size() == 1)
					action = BattleAction::makeTargetedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front());
				else if(castToPerform.commandTargets.size() == 2)
					action = BattleAction::makePairedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front(), castToPerform.commandTargets.back());
				else
					action = BattleAction::makeHeroCommand(side, castToPerform.command);
				cb->battleMakeSpellAction(battleID, action);
			}
			activeActionMade = true;
			return true;
		}
		BattleAction spellcast;
		spellcast.actionType = EActionType::HERO_SPELL;
		spellcast.spell = castToPerform.spell->id;
		spellcast.spellOvercharge = castToPerform.spellOvercharge;
		spellcast.spellSelectiveDispel = castToPerform.spellSelectiveDispel;
		spellcast.spellCureAffliction = castToPerform.spellCureAffliction;
		spellcast.spellMassSlow = castToPerform.spellMassSlow;
		spellcast.metamagicFollowup = castToPerform.metamagicFollowup;
		if(isCanonicalFireWall(*cb->getBattle(battleID), castToPerform.spell)
			&& castToPerform.spellFireWallDirection != BattleHex::NONE
			&& !castToPerform.dest.empty())
		{
			// Evaluation keeps the complete footprint so delayed damage and
			// friendly exposure can be scored.  The wire action intentionally
			// carries only the start hex plus the direction; the server expands
			// and validates the line authoritatively.
			spellcast.aimToHex(castToPerform.dest.front().hexValue);
			spellcast.spellFireWallDirection = castToPerform.spellFireWallDirection;
		}
		else
			spellcast.setTarget(castToPerform.dest);
		spellcast.side = side;
		spellcast.stackNumber = -1;
		cb->battleMakeSpellAction(battleID, spellcast);
		activeActionMade = true;

		return true;
	}

	LOGFL("Best hero action is %s. But it is actually useless (value %d).", castToPerform.name() % castToPerform.value);

	return false;
}

//Below method works only for offensive spells
void BattleEvaluator::evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps)
{
	using ValueMap = PossibleSpellcast::ValueMap;

	RNGStub rngStub;
	HypotheticBattle state(env.get(), cb->getBattle(battleID));
	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	ValueMap healthOfStack;
	ValueMap newHealthOfStack;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
	}


	spells::BattleCast cast(&state, stack, spells::Mode::CREATURE_ACTIVE, ps.spell);
	cast.castEval(state.getServerCallback(), ps.dest);

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);
		newHealthOfStack[unitId] = localUnit->getAvailableHealth();
	}

	int64_t totalGain = 0;

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);

		auto healthDiff = newHealthOfStack[unitId] - healthOfStack[unitId];

		if(state.battleGetOwner(localUnit) != playerID)
			healthDiff = -healthDiff;

		if(healthDiff < 0)
		{
			ps.value = -1;
			return; //do not damage own units at all
		}

		totalGain += healthDiff;
	}

	// consider the case in which spell summons units
	auto newUnits = state.getUnitsIf([&](const battle::Unit * u) -> bool
		{
			return !u->isGhost() && !u->isTurret() && !vstd::contains(healthOfStack, u->unitId());
		});

	for(auto unit : newUnits)
	{
		const auto health = unit->getAvailableHealth();
		totalGain += state.battleGetOwner(unit) == playerID ? health : -health;
	}

	ps.value = totalGain;
}

void BattleEvaluator::print(const std::string & text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}
