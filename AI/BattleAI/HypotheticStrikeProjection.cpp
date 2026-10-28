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
#include "HypotheticBattle.h"
#include "NewHorizonsHexOfPain.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
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
		token.recordStrike(capturedLuckSerendipityOrdinaryAttack.value_or(!attack.secondaryAttack
			&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage)),
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

std::optional<newHorizonsElementalRebirth::DeathSnapshot> HypotheticBattle::captureElementalRebirthSource(
	const battle::Unit & unit) const
{
	const auto side = unit.unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	return newHorizonsElementalRebirth::captureDeathSource(
		unit, getSideHero(side), getRebirthChainUsed(side),
		&getCreatureCategoryRules(), getPhoenixSparkUsed(side));
}

bool HypotheticBattle::hasReadyNativeRebirth(const battle::Unit * unit) const
{
	return unit && !unit->isClone()
		&& unit->valOfBonuses(BonusType::REBIRTH) > 0
		&& unit->canCast()
		&& unit->getPhantomInitialIntegrity() == 0;
}

void HypotheticBattle::projectPrimalBurst(const std::shared_ptr<StackWithBonuses> & projected)
{
	const auto hostileIds = newHorizonsElementalRebirth::adjacentHostileUnitIds(*this, *projected);
	const auto damageBudget = newHorizonsElementalRebirth::primalBurstDamageBudget(
		projected->getAvailableHealth());
	const auto rawShare = newHorizonsElementalRebirth::primalBurstShare(damageBudget, hostileIds.size());
	if(rawShare > 0)
	{
		struct PendingBurstHit
		{
			uint32_t unitId;
			std::shared_ptr<StackWithBonuses> preHitTarget;
			PlayerColor targetController;
			int64_t adjustedDamage;
			int64_t healthBefore;
			std::optional<newHorizonsElementalRebirth::DeathSnapshot> rebirthSource;
			bool cloneKilled;
			bool nativeRebirth;
		};
		std::vector<PendingBurstHit> pendingHits;
		pendingHits.reserve(hostileIds.size());

		// Match the authoritative batch: resolve every target, mitigation value,
		// controller, and death-source snapshot before changing any recipient.
		for(const auto targetId : hostileIds)
		{
			auto target = getForUpdate(targetId);
			if(!target || !target->alive())
				continue;
			const auto adjustedDamage = newHorizonsMagicalAbilityDamage::adjustDamage(
				*this, *target, rawShare);
			if(adjustedDamage <= 0)
				continue;
			auto preHitTarget = std::make_shared<StackWithBonuses>(this, target.get());
			preHitTarget->freezeDamageScoringBonusSnapshot();
			pendingHits.push_back({targetId, std::move(preHitTarget), battleGetOwner(target.get()),
				adjustedDamage, target->getAvailableHealth(), captureElementalRebirthSource(*target),
				target->isClone(), hasReadyNativeRebirth(target.get())});
		}

		for(const auto & hit : pendingHits)
		{
			auto target = getForUpdate(hit.unitId);
			if(!target || !target->alive())
				continue;
			auto appliedDamage = hit.adjustedDamage;
			target->damage(appliedDamage, false, battle::DamageProvenance::SPELL);
			const auto actualDamage = std::max<int64_t>(0,
				hit.healthBefore - target->getAvailableHealth());
			if(actualDamage <= 0)
				continue;
			target->removeUnitBonus(Bonus::UntilBeingAttacked);
			projectedPrimalBurstHits.push_back({hit.preHitTarget, hit.targetController, actualDamage});
		}

		// Only after the complete damage batch do its deaths trigger new rebirths.
		for(const auto & hit : pendingHits)
		{
			if(!hit.rebirthSource)
				continue;
			auto target = getForUpdate(hit.unitId);
			projectElementalRebirth(target.get(), *hit.rebirthSource,
				!target || !target->alive(), hit.cloneKilled, hit.nativeRebirth);
		}
	}
}

std::optional<uint32_t> HypotheticBattle::projectElementalRebirth(const battle::Unit * postHitUnit,
	const newHorizonsElementalRebirth::DeathSnapshot & captured, const bool hitKilled,
	const bool cloneKilled, const bool nativeRebirth)
{
	auto snapshot = captured;
	if(!newHorizonsElementalRebirth::stillEligibleDeath(
		postHitUnit, snapshot, hitKilled, cloneKilled, nativeRebirth,
		getRebirthChainUsed(snapshot.side)))
		return {};

	snapshot.phoenixSpark = snapshot.phoenixSpark && !getPhoenixSparkUsed(snapshot.side);
	if(snapshot.phoenixSpark)
	{
		BattleUnitsChanged receipt;
		receipt.battleID = getBattleID();
		receipt.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{snapshot.side, snapshot.unitId};
		getServerCallback()->apply(receipt);
	}
	const auto candidates = newHorizonsElementalRebirth::legalCandidatePool(
		*this, getAccessibility(), snapshot);
	if(candidates.empty())
		return {};

	// This is a private representative outcome for one hypothetical branch. Never draw
	// from the authoritative game RNG or mutate the live battle's random stream.
	size_t candidateIndex = 0;
	if(candidates.size() > 1)
	{
		auto * rng = getServerCallback()->getRNG();
		if(!rng)
			return {};
		candidateIndex = static_cast<size_t>(rng->nextInt(static_cast<int>(candidates.size()) - 1));
	}
	if(candidateIndex >= candidates.size())
		return {};
	const auto creature = candidates[candidateIndex];
	const auto * sourceArmy = getSideArmy(snapshot.side);
	const auto owner = getSidePlayer(snapshot.side);
	const auto effectiveMaxHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
		sourceArmy, creature, owner, snapshot.side);
	auto descriptor = newHorizonsElementalRebirth::makeSpawnDescriptor(nextUnitId(), creature,
		snapshot.side, snapshot.corpsePosition, newHorizonsElementalRebirth::targetHP(snapshot, creature, *this),
		effectiveMaxHP, snapshot.chain);
	if(!descriptor)
		return {};

	if(snapshot.chain)
	{
		// The shared ADD visitor reserves this branch's quota before publishing the
		// second generation. Nested Primal Burst deaths must observe the spent use.
		BattleUnitsChanged add;
		add.battleID = getBattleID();
		add.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
			snapshot.side, snapshot.unitId, descriptor->unit.id};
		auto & added = add.changedStacks.emplace_back(descriptor->unit.id, UnitChanges::EOperation::ADD);
		descriptor->unit.save(added.data);
		getServerCallback()->apply(add);
	}
	else
	{
		JsonNode data;
		descriptor->unit.save(data);
		addUnit(descriptor->unit.id, data);
	}
	if(const auto ward = newHorizonsElementalRebirth::elementalWardBonus(snapshot.profile))
		addUnitBonus(descriptor->unit.id, {*ward});
	auto projected = getForUpdate(descriptor->unit.id);
	const auto fullHealth = projected->getAvailableHealth();
	if(fullHealth < descriptor->health.targetAggregateHP)
	{
		removeUnit(descriptor->unit.id);
		if(snapshot.chain)
			setRebirthChainUsed(snapshot.side, false);
		return {};
	}
	const auto wound = fullHealth - descriptor->health.targetAggregateHP;
	if(wound > 0)
	{
		auto appliedDamage = wound;
		projected->damage(appliedDamage);
	}
	if(projected->getAvailableHealth() != descriptor->health.targetAggregateHP)
	{
		removeUnit(descriptor->unit.id);
		if(snapshot.chain)
			setRebirthChainUsed(snapshot.side, false);
		return {};
	}
	elementalRebirthSpawnUnitIds.insert(descriptor->unit.id);
	if(snapshot.profile.swiftRebirth)
		addUnitBonus(descriptor->unit.id, {newHorizonsSwiftRebirth::marker(getRound())});
	const auto memory = newHorizonsElementalRebirth::elementalMemoryBonuses(snapshot, *projected);
	if(!memory.empty())
		addUnitBonus(descriptor->unit.id, memory);

	if(snapshot.profile.primalBurst)
		projectPrimalBurst(projected);
	return descriptor->unit.id;
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
	const auto rebirthSource = attacker->alive()
		? captureElementalRebirthSource(*attacker) : std::nullopt;
	const bool nativeRebirth = hasReadyNativeRebirth(attacker.get());
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
	if(rebirthSource)
		projectElementalRebirth(attacker.get(), *rebirthSource,
			attacker->alive() == false, attacker->isClone(), nativeRebirth);

	return std::max<int64_t>(0, healthBefore - attacker->getAvailableHealth());
}
