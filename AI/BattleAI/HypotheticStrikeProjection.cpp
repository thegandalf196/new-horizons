/*
 * HypotheticStrikeProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhysicalStrikeProjection.h"
#include "HypotheticBattle.h"
#include "NewHorizonsHexOfPain.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

namespace
{
bool isHexOfPainBonus(const Bonus & bonus)
{
	if(bonus.type != BonusType::COMBAT_EVENT_TRIGGER || bonus.source != BonusSource::SPELL_EFFECT
		|| !bonus.parameters || bonus.sid.toString() != newHorizonsHexOfPainAI::SPELL_ID)
		return false;

	if(bonus.subtype.toString() != newHorizonsHexOfPainAI::TRIGGER_ID)
		return false;

	const auto & parameters = bonus.parameters->toCustom<JsonNode>();
	return parameters["damageSharePercent"].isNumber()
		&& !parameters["casterSide"].isNull();
}
}

bool newHorizonsHexOfPainAI::hasEffect(const battle::Unit * unit)
{
	if(!unit)
		return false;

	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	return std::ranges::any_of(*triggers, [](const auto & bonus)
	{
		return bonus && isHexOfPainBonus(*bonus);
	});
}

int newHorizonsHexOfPainAI::effectRounds(const battle::Unit * unit)
{
	if(!unit)
		return 0;

	int rounds = 0;
	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
		if(bonus && isHexOfPainBonus(*bonus) && Bonus::NTurns(bonus.get()))
			rounds = std::max(rounds, static_cast<int>(bonus->turnsRemain));
	return std::max(0, rounds);
}

bool HypotheticBattle::fortuneStrikeIsCertain(const BattleAttackInfo & attack) const
{
	if(battleCanTriggerPerfectFortune(attack))
		return true;
	if(attack.luckyStrike)
		return true;

	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return false;

	const auto rules = getLuckRollRules();
	if(rules.diceSize <= 0)
		return false;

	const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
	if(chances.empty())
		return false;
	const auto index = std::min<size_t>(static_cast<size_t>(std::abs(luck)), chances.size()) - 1;
	return chances[index] >= rules.diceSize;
}

ProjectedLuckOutcome HypotheticBattle::captureFortuneStrikeOutcome(const BattleAttackInfo & attack) const
{
	if(battleCanTriggerPerfectFortune(attack))
		return ProjectedLuckOutcome::POSITIVE;
	if(attack.luckyStrike)
		return ProjectedLuckOutcome::POSITIVE;
	if(attack.unluckyStrike)
		return ProjectedLuckOutcome::NEGATIVE;

	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return ProjectedLuckOutcome::NEUTRAL;
	const auto rules = getLuckRollRules();
	if(rules.diceSize > 0)
	{
		const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
		if(!chances.empty())
		{
			const auto index = std::min<size_t>(static_cast<size_t>(std::abs(luck)), chances.size()) - 1;
			if(chances[index] <= 0)
				return ProjectedLuckOutcome::NEUTRAL;
			if(chances[index] >= rules.diceSize)
				return luck > 0 ? ProjectedLuckOutcome::POSITIVE : ProjectedLuckOutcome::NEGATIVE;
		}
	}
	return ProjectedLuckOutcome::UNKNOWN;
}

void HypotheticBattle::projectFrozenShatter(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits)
{
	for(const auto & [id, damage] : hits)
	{
		auto target = getForUpdate(id);
		auto hit = attack;
		hit.defender = target.get();
		if(newHorizonsFrozen::qualifiesForShatter(hit, getMagicRules()))
			target->removeUnitBonus(newHorizonsFrozen::removalPlan(*target));
	}
}

void HypotheticBattle::projectFortuneStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits,
	battle::CUnitState * attackerState, bool enemyStackKilled,
	std::optional<ProjectedLuckOutcome> resolvedLuck, bool applyAftermath,
	std::optional<bool> capturedPerfectFortune, BattleSide capturedPerfectFortuneSide,
	BattleSide capturedLuckSerendipitySide, std::optional<bool> capturedLuckSerendipityOrdinaryAttack)
{
	if(applyAftermath)
		projectFrozenShatter(attack, hits);
	const auto serendipitySide = capturedLuckSerendipitySide == BattleSide::ATTACKER
		|| capturedLuckSerendipitySide == BattleSide::DEFENDER
		? capturedLuckSerendipitySide : playerToSide(battleGetActionController(attack.attacker));
	// Resolve before consuming the one-strike bonus; UNKNOWN remains unknown.
	const auto serendipityOutcome = resolvedLuck ? *resolvedLuck : captureFortuneStrikeOutcome(attack);
	if(serendipitySide == BattleSide::ATTACKER || serendipitySide == BattleSide::DEFENDER)
	{
		auto token = luckSerendipityStates.at(serendipitySide);
		token.recordStrike(capturedLuckSerendipityOrdinaryAttack ? *capturedLuckSerendipityOrdinaryAttack
			: !attack.secondaryAttack
				&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage),
			serendipityOutcome == ProjectedLuckOutcome::POSITIVE);
		setLuckSerendipityState(serendipitySide, token);
	}
	const auto perfectFortuneSide = capturedPerfectFortune.value_or(false)
		&& (capturedPerfectFortuneSide == BattleSide::ATTACKER || capturedPerfectFortuneSide == BattleSide::DEFENDER)
		? capturedPerfectFortuneSide : playerToSide(battleGetActionController(attack.attacker));
	const bool perfectFortune = capturedPerfectFortune ? *capturedPerfectFortune : battleCanTriggerPerfectFortune(attack);
	if(perfectFortune && (perfectFortuneSide == BattleSide::ATTACKER || perfectFortuneSide == BattleSide::DEFENDER)
		&& perfectFortuneStates.at(perfectFortuneSide).available())
		perfectFortuneStates.at(perfectFortuneSide).used = true;
	// Existing Luck aftermath follows allegiance, while the new guarantee token
	// belongs to the current action controller, as in the accepted live strike.
	const auto side = playerToSide(battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	auto & fortune = fortuneStates.at(side);
	// Only Sylvan Lucky Recovery heals a creature. Generic Luck now operates
	// on accepted hero actions; record-only projections must never heal.
	if(applyAftermath && !attack.shooting && attackerState && attackerState->alive()
		&& (perfectFortune || serendipityOutcome == ProjectedLuckOutcome::POSITIVE)
		&& fortune.luckyRecovery)
	{
		int64_t actualDamage = 0;
		const auto rules = getLuckRollRules();
		for(const auto & [unitId, damage] : hits)
			if((attack.defender && unitId == attack.defender->unitId()) || rules.affectsAllTargets)
				actualDamage += std::max<int64_t>(0, damage);
		auto healing = newHorizonsCombatSkills::luckyRecoveryAmount(actualDamage,
			false, fortune.luckyRecovery);
		if(healing > 0)
			attackerState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
	if(!fortune.active())
		return;

	const auto rules = getLuckRollRules();
	const bool gamblerAttackAvailable = fortune.gamblerAttackAvailable();
	const bool chainFortuneAvailable = fortune.chainFortuneAvailable(attack.attacker->unitId());
	// Resolve this attack with the pre-consumption Luck snapshot. Candidate
	// metadata carries that result through replay after one-strike bonuses expire.
	const auto outcome = perfectFortune ? ProjectedLuckOutcome::POSITIVE
		: (resolvedLuck ? *resolvedLuck : captureFortuneStrikeOutcome(attack));
	const bool positive = outcome == ProjectedLuckOutcome::POSITIVE;
	const bool negative = outcome == ProjectedLuckOutcome::NEGATIVE;
	const bool guaranteedNonPositive = outcome == ProjectedLuckOutcome::NEGATIVE
		|| outcome == ProjectedLuckOutcome::NEUTRAL;
	// Chain consumes its carried benefit on the next different friendly stack's
	// attack, even when the pre-consumption Luck outcome is still stochastic.
	if(!positive && !negative && !gamblerAttackAvailable && !chainFortuneAvailable)
		return;

	// Negative Providence history is committed as well, but it has no
	// aftermath to project.  recordStrike returns true when that bad result was
	// suppressed by Providence, exactly as it does in the authoritative path.
	// Gambler and an eligible Chain gift also record unresolved strikes so the
	// candidate and committed branches consume their windows without guessing
	// the stochastic Luck result.
	const bool ignoredNegative = fortune.recordStrike(attack.attacker->unitId(), positive, negative,
		newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage));
	if(gamblerAttackAvailable && guaranteedNonPositive)
	{
		static const CSelector gamblerPenaltySelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		});
		auto projectedAttacker = getForUpdate(attack.attacker->unitId());
		if(!projectedAttacker->hasBonus(gamblerPenaltySelector))
			addUnitBonus(attack.attacker->unitId(), {newHorizonsCombatSkills::gamblerLuckPenalty()});
	}
	if(ignoredNegative || !applyAftermath)
		return;
	if(!positive)
		return;

	std::vector<uint32_t> adjacentFriends = battleFortuneAdjacentFriends(attack.attacker);
	for(const auto & [unitId, damage] : hits)
	{
		const bool primary = unitId == attack.defender->unitId();
		if(!primary && !rules.affectsAllTargets)
			continue;
		const auto * target = battleGetUnitByID(unitId);
		if(target && !target->alive())
			vstd::erase(adjacentFriends, unitId);
	}

	fortune.finishPositiveStrike(adjacentFriends, enemyStackKilled);
}


bool HypotheticBattle::hasReadyNativeRebirth(const battle::Unit * unit) const
{
	return unit && !unit->isClone()
		&& unit->valOfBonuses(BonusType::REBIRTH) > 0
		&& unit->canCast()
		&& unit->getPhantomInitialIntegrity() == 0;
}



void HypotheticBattle::projectRangedMarkStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits)
{
	if(!attack.shooting || !attack.attacker || !attack.defender || hits.empty()
		|| !newHorizonsMagic::rulesActive(getMagicRules()))
		return;

	const auto attacker = getForUpdate(attack.attacker->unitId());
	CombatEventPayload payload;
	payload.ranged = true;
	payload.isCounter = attack.retaliation;
	for(const auto & [unitId, damage] : hits)
	{
		if(damage <= 0)
			continue;
		const auto * target = battleGetUnitByID(unitId);
		if(!target)
			continue;
		AttackedTarget hit;
		hit.unit = target;
		hit.damage = damage;
		payload.targets.push_back(hit);
	}
	if(payload.targets.empty())
		return;

	const auto triggers = attacker->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
	{
		if(bonus->source != BonusSource::SPELL_EFFECT || !bonus->parameters
			|| bonus->sid.toString() != newHorizonsSorcery::FOCUS_MAGIC_SPELL)
			continue;
		const auto scriptId = bonus->subtype.as<ScriptID>();
		const auto & script = LIBRARY->scriptTypes()->getById(scriptId);
		if(script.scriptId != newHorizonsSorcery::FOCUS_MAGIC_TRIGGER || !script.combatEventScript)
			continue;
		JsonNode parameters = bonus->parameters->toCustom<JsonNode>();
		parameters["val"].Integer() = bonus->val;
		script.combatEventScript->run(getServerCallback(), *this, CombatEventType::AFTER_ATTACK,
			attacker.get(), battleGetUnitByID(attack.defender->unitId()), parameters, payload);
	}
}

int64_t HypotheticBattle::projectHexOfPainStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits, int32_t attackIndex)
{
	if(!attack.attacker || hits.empty())
		return 0;

	const auto attacker = getForUpdate(attack.attacker->unitId());
	const auto triggers = attacker->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	if(triggers->empty())
		return 0;
	const auto healthBefore = attacker->getAvailableHealth();
	CombatEventPayload payload;
	payload.ranged = attack.shooting;
	payload.isCounter = attack.retaliation;
	payload.attackIndex = attackIndex;
	for(const auto & [unitId, damage] : hits)
	{
		const auto * target = battleGetUnitByID(unitId);
		if(!target)
			continue;

		AttackedTarget hit;
		hit.unit = target;
		hit.damage = std::max<int64_t>(0, damage);
		// `hits` already contains post-cap damage from the projected attack. Keep
		// the script's health cap meaningful without needing a second health
		// snapshot at this post-attack callback boundary.
		hit.healthBeforeAttack = hit.damage;
		payload.targets.push_back(hit);
	}
	if(payload.targets.empty())
		return 0;

	for(const auto & bonus : *triggers)
	{
		if(!bonus || !isHexOfPainBonus(*bonus))
			continue;

		const auto scriptId = bonus->subtype.as<ScriptID>();
		const auto & script = LIBRARY->scriptTypes()->getById(scriptId);
		if(!script.combatEventScript)
			continue;

		JsonNode parameters = bonus->parameters->toCustom<JsonNode>();
		parameters["val"].Integer() = bonus->val;
		script.combatEventScript->run(getServerCallback(), *this, CombatEventType::AFTER_ATTACK,
			attacker.get(), attack.defender ? battleGetUnitByID(attack.defender->unitId()) : nullptr,
			parameters, payload);
	}

	return std::max<int64_t>(0, healthBefore - attacker->getAvailableHealth());
}
