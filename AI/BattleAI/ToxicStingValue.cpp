/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ToxicStingValue.h"
#include "AttackPossibility.h"
#include "AttackAbilityPrefix.h"
#include "NominalAbilityChance.h"
#include "PhysicalPoisonValue.h"
#include "../../lib/battle/AttackAbilityQueries.h"
#include "../../lib/battle/CreatureAbilityChance.h"
#include "../../lib/battle/NewHorizonsToxicSting.h"
#include "../../lib/battle/BodyHealthLoss.h"
#include <algorithm>
namespace toxicStingAI
{
namespace
{
Budget budget(const battle::CUnitState & unit)
{
	return {unit.physicalPoisonBaseDamage, unit.physicalPoisonActivationsRemaining,
		unit.physicalPoisonSourceStackId};
}
void prefix(std::map<Outcome, double> & outcomes, const creatureAbilityChance::Rounding & chance)
{
	std::map<Outcome, double> next;
	for(const auto & [state, mass] : outcomes)
	{
		const auto transition = nominalAbilityChance::harmful(chance, state.available);
		for(int i = 0; i < 4; ++i)
			if(transition[i] > 0)
			{
				auto result = state;
				result.available = i < 2;
				next[std::move(result)] += mass * transition[i];
			}
	}
	outcomes = std::move(next);
}
void observe(std::map<Outcome, double> & outcomes, std::map<uint32_t, Budget> & observed,
	const battle::CUnitState & unit)
{
	const auto actual = budget(unit);
	const auto previous = observed.find(unit.unitId());
	const bool changed = previous != observed.end() && previous->second != actual;
	std::map<Outcome, double> next;
	for(const auto & [state, mass] : outcomes)
	{
		auto result = state;
		auto [entry, inserted] = result.budgets.try_emplace(unit.unitId(), actual);
		// A deterministic Poison update is real in every nominal outcome. A
		// weaker update cannot replace a stronger hypothetical application.
		if(!inserted && changed && (actual.base == 0 || actual.base >= entry->second.base))
			entry->second = actual;
		next[std::move(result)] += mass;
	}
	outcomes = std::move(next);
	observed[unit.unitId()] = actual;
}
float utility(const std::shared_ptr<CBattleInfoCallback> & battle, const battle::Unit * source,
	const battle::Unit & target, const Budget & poison, DamageCache & cache)
{
	const auto query = target.acquireState();
	query->physicalPoisonBaseDamage = poison.base;
	query->physicalPoisonActivationsRemaining = poison.remaining;
	query->physicalPoisonSourceStackId = poison.source;
	return AttackPossibility::calculateDamageReduce(source, &target,
		physicalPoisonValue::projectedDamage(query.get()), cache, battle);
}
float attempt(std::map<Outcome, double> & outcomes, const newHorizonsToxicSting::Plan & plan,
	const creatureAbilityChance::Rounding & chance, const std::shared_ptr<CBattleInfoCallback> & battle,
	const battle::Unit & source, const battle::Unit & target, DamageCache & cache, bool measure)
{
	std::map<Outcome, double> next;
	double value = 0;
	for(const auto & [state, mass] : outcomes)
	{
		const auto before = state.budgets.at(plan.target);
		if(plan.baseDamage < before.base)
		{
			next[state] += mass; // Native skips the harmful roll for weaker Poison.
			continue;
		}
		const Budget after{plan.baseDamage, 3, static_cast<int32_t>(plan.source)};
		const auto increment = measure ? std::max(0.0f, utility(battle, &source, target, after, cache)
			- utility(battle, &source, target, before, cache)) : 0;
		const auto transition = nominalAbilityChance::harmful(chance, state.available);
		for(int i = 0; i < 4; ++i)
			if(transition[i] > 0)
			{
				auto result = state;
				result.available = i < 2;
				if(i % 2)
				{
					result.budgets[plan.target] = after;
					value += mass * transition[i] * increment;
				}
				next[std::move(result)] += mass * transition[i];
			}
	}
	outcomes = std::move(next);
	return static_cast<float>(value);
}
}
bool requiresPreview(const CBattleInfoCallback & battle, const battle::Unit & source,
	const battle::Unit & requested, const battle::Unit & resolved)
{
	const auto pair = [&](const battle::Unit & target)
	{
		return newHorizonsToxicSting::chancePercent(battle, BattleAttackInfo(&source, &target, 0, false)) > 0
			|| newHorizonsToxicSting::chancePercent(battle, BattleAttackInfo(&target, &source, 0, false)) > 0;
	};
	return pair(resolved) || (&requested != &resolved && pair(requested));
}
std::optional<newHorizonsToxicSting::Plan> prepare(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t incoming, battle::DamageProvenance provenance)
{
	const auto * target = dynamic_cast<const battle::CUnitState *>(attack.defender);
	if(!target)
		return std::nullopt;
	const auto applied = battleAIProjectDamage(target, incoming, provenance).appliedDamage;
	auto physical = attack;
	physical.physicalDamage = attack.physicalDamage && provenance == battle::DamageProvenance::PHYSICAL_CREATURE;
	return newHorizonsToxicSting::prepare(battle, physical,
		battle::bodyHealthLoss(applied, target->health.getTemporaryHitPoints()));
}
float Opportunities::value(const std::shared_ptr<HypotheticBattle> & battle,
	const BattleAttackInfo & attack, int64_t bodyLoss, DamageCache & cache)
{
	return battle ? preparedValue(battle, attack, newHorizonsToxicSting::prepare(*battle, attack, bodyLoss), cache) : 0;
}
float Opportunities::preparedValue(const std::shared_ptr<HypotheticBattle> & battle,
	const BattleAttackInfo & attack, const std::optional<newHorizonsToxicSting::Plan> & plan, DamageCache & cache, bool measure)
{
	if(!battle || !plan || !attack.attacker || !attack.defender || !attack.attacker->alive()
		|| attack.attacker->unitId() != plan->source || attack.defender->unitId() != plan->target
		|| battle->battleGetOwner(attack.attacker) == battle->battleGetOwner(attack.defender))
		return 0;
	// Authority has already committed afterAttack before passive casts. Preview
	// reactions occur earlier, so query that transition on a detached source.
	// Spell mechanics resolve explicit recipients through the battle roster.
	// Keep the transitioned temporary units in one private query branch on both
	// routes; export-backed removals also require this fork for source isolation.
	// Fork once for the marginal query, never per outcome.
	const bool owned = static_cast<bool>(battle->initialExportSubject());
	const auto queryBattle = std::make_shared<HypotheticBattle>(battle->env, battle);
	const auto source = owned ? queryBattle->getForUpdate(plan->source)
		: std::make_shared<StackWithBonuses>(queryBattle.get(), attack.attacker);
	auto target = owned ? queryBattle->getForUpdate(plan->target)
		: std::shared_ptr<StackWithBonuses>{};
	if(owned)
	{
		*source = *attack.attacker->acquireState();
		*target = *attack.defender->acquireState();
	}
	queryBattle->stackStates[plan->source] = source;
	source->afterAttack(attack.shooting, attack.retaliation, attack.physicalDamage);
	source->removeUnitBonus(Bonus::UntilAttack);
	if(!attack.retaliation)
		source->removeUnitBonus(Bonus::UntilOwnAttack);
	if(!owned)
		target = std::make_shared<StackWithBonuses>(queryBattle.get(), attack.defender);
	queryBattle->stackStates[plan->target] = target;
	target->removeUnitBonus(Bonus::UntilBeingAttacked);
	if(!newHorizonsToxicSting::livingTarget(*target)
		|| queryBattle->battleGetOwner(source.get()) == queryBattle->battleGetOwner(target.get()))
		return 0;
	const auto info = attackAbilityPrefix::query(*queryBattle, *source, *target, attack.shooting);
	const auto side = queryBattle->playerToSide(queryBattle->battleGetOwner(target.get()));
	auto [entry, inserted] = ledgers.try_emplace(side);
	auto & ledger = entry->second;
	if(inserted)
		ledger.outcomes[Outcome{info.initialNominalRerollAvailability > 0, {}}] = 1;
	if(!inserted && info.adverseFactVisible && info.initialNominalRerollAvailability == 0)
	{
		std::map<Outcome, double> spent;
		for(const auto & [state, mass] : ledger.outcomes)
		{
			auto result = state; result.available = false; spent[std::move(result)] += mass;
		}
		ledger.outcomes = std::move(spent);
	}
	observe(ledger.outcomes, ledger.observed, *target);
	ledger.sources.try_emplace(plan->target, plan->source);
	// Only current native eligibility is queried. Counterfactual effects of
	// unrelated passive spells and future revival choices remain unvalued.
	for(const auto id : info.eligibleHarmfulAttempts)
		prefix(ledger.outcomes, creatureAbilityChance::rounding(*source,
			attackAbilityQueries::parameters(*source, BonusType::SPELL_AFTER_ATTACK, id, attack.shooting).chance));
	return attempt(ledger.outcomes, *plan, creatureAbilityChance::rounding(*source, plan->chancePercent),
		queryBattle, *source, *target, cache, measure);
}
void Opportunities::score(AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & battle,
	const BattleAttackInfo & attack, const std::optional<newHorizonsToxicSting::Plan> & plan, DamageCache & cache)
{
	preparedValue(battle, attack, plan, cache, false);
}
void Opportunities::complete(AttackPossibility & preview, const std::shared_ptr<CBattleInfoCallback> & battle,
	DamageCache & cache)
{
	const auto recipient = [&](uint32_t id) -> const battle::CUnitState *
	{
		if(preview.attackerState && preview.attackerState->unitId() == id)
			return preview.attackerState.get();
		for(const auto & unit : preview.affectedUnits)
			if(unit && unit->unitId() == id)
				return unit.get();
		return nullptr;
	};
	for(auto & [side, ledger] : ledgers)
		for(const auto & [id, sourceId] : ledger.sources)
		{
			const auto * target = recipient(id);
			if(!target || !newHorizonsToxicSting::livingTarget(*target))
				continue;
			const auto * source = recipient(sourceId);
			if(!source)
				source = dynamic_cast<const battle::CUnitState *>(battle->battleGetUnitByID(sourceId));
			if(source && !source->alive())
				source = nullptr; // Existing utility owner selects another living reference.
			observe(ledger.outcomes, ledger.observed, *target);
			const auto before = budget(*target);
			const float baseline = utility(battle, source, *target, before, cache);
			std::map<Budget, double> marginal;
			for(const auto & [outcome, mass] : ledger.outcomes)
				marginal[outcome.budgets.at(id)] += mass;
			double weighted = 0;
			for(const auto & [poison, mass] : marginal)
				weighted += mass * std::max(0.0f, utility(battle, source, *target, poison, cache) - baseline);
			if(battle->battleMatchActionController(preview.attack.attacker, target))
				preview.defenderDamageReduce += static_cast<float>(weighted);
			else
				preview.attackerDamageReduce += static_cast<float>(weighted);
		}
	ledgers.clear(); // A repeated completion cannot price this opportunity twice.
}
void Opportunities::scoreCounter(AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & battle,
	const battle::Unit & source, const battle::Unit & target,
	const std::optional<newHorizonsToxicSting::Plan> & plan, bool physical, DamageCache & cache)
{
	BattleAttackInfo counter(&source, &target, 0, false);
	counter.retaliation = true;
	counter.physicalDamage = physical;
	score(preview, battle, counter, plan, cache);
}

}
