/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpecialtyProcValue.h"
#include "AttackAbilityPrefix.h"
#include "NominalAbilityChance.h"
#include "CreatureActivationValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsSpecialtyProcs.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusList.h"
#include <algorithm>
namespace specialtyProcAI
{
namespace
{
float armyPressure(const std::shared_ptr<CBattleInfoCallback> & board, PlayerColor reference, DamageCache & cache)
{
	float result = 0;
	for(const auto * unit : board->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTimeStopped()
			|| newHorizonsFrozen::isFrozen(*unit) || newHorizonsStun::isStunned(*unit)) continue;
		const auto pressure = creatureActivationValue::expectedTargetActivationValue(unit, cache, board, true);
		result += board->battleGetOwner(unit) == reference ? pressure : -pressure;
	}
	return result;
}
}
void Opportunities::transition(Ledger & ledger, const battle::Unit & source,
	int chance, bool harmful, const std::optional<Key> & key, const std::optional<attackAbilityPrefix::StatusKey> & condition)
{
	std::map<Outcome, double> next;
	for(const auto & [state, mass] : ledger.outcomes)
	{
		if(condition && attackAbilityPrefix::alreadyApplied(state.completedStatuses, *condition))
		{
			next[state] += mass;
			continue;
		}
		const auto rounding = creatureAbilityChance::rounding(source, chance);
		const auto probabilities = nominalAbilityChance::harmful(rounding, harmful && state.rerollAvailable);
		for(size_t index = 0; index < probabilities.size(); ++index)
			if(probabilities[index] > 0)
			{
				auto result = state;
				if(harmful) result.rerollAvailable = index < 2;
				if(index % 2)
				{
					if(key) result.applied.insert(*key);
					if(condition) result.completedStatuses.insert(*condition);
				}
				next[std::move(result)] += mass * probabilities[index];
			}
	}
	ledger.outcomes = std::move(next);
}
bool Opportunities::requiresPreview(const CBattleInfoCallback & board, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender) return false;
	const auto pair = [&](const battle::Unit & source, const battle::Unit & target, bool ranged)
	{
		return !newHorizonsSpecialtyProcs::additionalAttackSpells(board, source, ranged).empty()
			|| newHorizonsSpecialtyProcs::poisonPotencyMultiplier(board, source) > 1
			|| newHorizonsSpecialtyProcs::stunChance(board, BattleAttackInfo(&source, &target, 0, ranged)) > 0;
	};
	return pair(*attack.attacker, *attack.defender, attack.shooting)
		|| pair(*attack.defender, *attack.attacker, false);
}
void Opportunities::observe(const std::shared_ptr<HypotheticBattle> & board, const BattleAttackInfo & attack, bool includeStun)
{
	if(!board || !attack.attacker || !attack.defender || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.secondaryAttack || attack.cleaveDamagePercent > 0) return;
	const bool specialtyStrike = !attack.bracePreemptive && attack.preemptiveDamagePercent == 0;
	const auto info = attackAbilityPrefix::query(*board, *attack.attacker, *attack.defender, attack.shooting, specialtyStrike);
	const auto side = board->playerToSide(board->battleGetOwner(attack.defender));
	auto [entry, inserted] = ledgers.try_emplace(side);
	auto & ledger = entry->second;
	if(inserted) ledger.outcomes[Outcome{info.initialNominalRerollAvailability > 0, {}, {}}] = 1;
	if(info.adverseFactVisible && info.initialNominalRerollAvailability == 0)
	{
		std::map<Outcome, double> spent;
		for(const auto & [state, mass] : ledger.outcomes)
		{
			auto value = state; value.rerollAvailable = false; spent[std::move(value)] += mass;
		}
		ledger.outcomes = std::move(spent);
	}
	for(const auto & attempt : info.attempts)
	{
		std::optional<Key> key;
		if(attempt.specialty && attempt.spell == SpellID::POISON)
		{
			key = Key{attack.defender->unitId(), Kind::POISON,
				newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*board, *attack.attacker), {}};
			ledger.effects[*key].incoming = newHorizonsSpecialtyProcs::poisonBonusPlan(*board, *attack.attacker, attempt.duration);
		}
		else if(attempt.specialty && attempt.spell == SpellID::DISPEL_HELPFUL_SPELLS)
		{
			Effect effect;
			Key selected{attack.defender->unitId(), Kind::DISPEL, 0, {}};
			const auto bonuses = attack.defender->getBonuses(Selector::all, "all");
			for(const auto & bonus : *bonuses)
				if(bonus && newHorizonsSpecialtyProcs::isPositiveTemporaryMagic(*bonus))
				{
					effect.selectedRecords.push_back(bonus); selected.selectedRecords.push_back(bonus.get());
				}
			if(!effect.selectedRecords.empty()) { key = selected; ledger.effects[*key] = std::move(effect); }
		}
		transition(ledger, *attack.attacker, attempt.chancePercent, attempt.harmful, key, attempt.condition);
	}
	if(includeStun && info.stunChancePercent > 0)
	{
		const Key key{attack.defender->unitId(), Kind::STUN, 0, {}};
		ledger.effects.try_emplace(key);
		transition(ledger, *attack.attacker, info.stunChancePercent, true, key, info.stunCondition);
	}
}
void Opportunities::observeStun(const std::shared_ptr<HypotheticBattle> & board, const BattleAttackInfo & attack)
{
	if(!board || !attack.attacker || !attack.defender || !attack.attacker->alive() || !attack.defender->alive()
		|| newHorizonsSpecialtyProcs::stunChance(*board, attack) == 0) return;
	const auto info = attackAbilityPrefix::query(*board, *attack.attacker, *attack.defender, attack.shooting);
	const auto side = board->playerToSide(board->battleGetOwner(attack.defender));
	auto [entry, inserted] = ledgers.try_emplace(side);
	auto & ledger = entry->second;
	if(inserted) ledger.outcomes[Outcome{info.initialNominalRerollAvailability > 0, {}, {}}] = 1;
	const Key key{attack.defender->unitId(), Kind::STUN, 0, {}};
	ledger.effects.try_emplace(key);
	transition(ledger, *attack.attacker, info.stunChancePercent, true, key, info.stunCondition);
}
void Opportunities::observeCounter(const std::shared_ptr<HypotheticBattle> & board,
	const battle::Unit & actor, const battle::Unit & target)
{
	BattleAttackInfo attack(&actor, &target, 0, false);
	attack.retaliation = true;
	observe(board, attack, false);
	if(board) observeBloodlust(board, newHorizonsAttackControllers::captureStrike(*board, attack, 0));
	observeStun(board, attack);
}

void Opportunities::observeBloodlust(const std::shared_ptr<HypotheticBattle> & board,
	const newHorizonsAttackControllers::Strike & receipt)
{
	if(!board || !receipt.bloodlust) return;
	const auto * source = board->battleGetUnitByID(receipt.source);
	if(!source || !source->alive() || source->isGhost() || source->isTimeStopped()) return;
	const auto pool = newHorizonsAttackControllers::bloodlustRecipients(*board, *source);
	if(pool.empty()) return;
	for(const auto id : pool)
	{
		const Key key{id, Kind::BLOODLUST, 0, {}};
		auto & [chance, effect] = bloodlust[key];
		chance += (1 - chance) / pool.size();
		auto & incoming = effect.incoming;
		incoming = newHorizonsAttackControllers::bloodlustBonusPlan();
		for(auto & bonus : incoming) bonus.spellCasterOwner = board->battleGetOwner(source);
	}
}

void Opportunities::complete(AttackPossibility & preview, const std::shared_ptr<CBattleInfoCallback> & board, DamageCache & cache)
{
	const auto parent = std::dynamic_pointer_cast<HypotheticBattle>(board);
	if(!parent) { ledgers.clear(); bloodlust.clear(); return; }
	std::set<uint32_t> recipients;
	for(const auto & [side, ledger] : ledgers)
		for(const auto & [key, effect] : ledger.effects)
			if(const auto * target = board->battleGetUnitByID(key.target); target && target->alive() && !target->isGhost())
				recipients.insert(key.target);
	for(const auto & [key, marginal] : bloodlust)
		if(const auto * target = board->battleGetUnitByID(key.target); target && target->alive() && !target->isGhost())
			recipients.insert(key.target);
	if(recipients.empty()) { ledgers.clear(); bloodlust.clear(); return; }
	const auto reference = board->battleGetActionController(preview.attack.attacker);
	// The probe changes ordinary spell bonuses, so ID-only inherited pairs are
	// not a certificate for its new stats. Keep the original cache only for the
	// established health-value baseline; pressure queries use fresh local pairs.
	DamageCache baselineCache;
	const auto baselinePressure = armyPressure(board, reference, baselineCache);
	auto probe = std::make_shared<HypotheticBattle>(parent->env, parent);
	double expected = 0;
	for(const auto & [side, ledger] : ledgers)
		for(const auto & [outcome, mass] : ledger.outcomes)
		{
			if(outcome.applied.empty()) continue;
			for(const auto id : recipients)
				probe->stackStates[id] = std::make_shared<StackWithBonuses>(probe.get(), board->battleGetUnitByID(id));
			++probe->bonusTreeVersion;
			for(const auto & key : outcome.applied)
			{
				if(!recipients.contains(key.target)) continue;
				const auto target = probe->getForUpdate(key.target);
				const auto & effect = ledger.effects.at(key);
				if(key.kind == Kind::POISON)
				{
					const auto refresh = newHorizonsSpecialtyProcs::poisonRefreshPlan(*target, effect.incoming);
					target->removeUnitBonus(refresh.remove); target->addUnitBonus(refresh.add);
				}
				else if(key.kind == Kind::DISPEL)
				{
					std::vector<Bonus> remove;
					const auto current = board->battleGetUnitByID(key.target)->getBonuses(Selector::all, "all");
					for(const auto & selected : effect.selectedRecords)
						if(std::ranges::find(*current, selected) != current->end()) remove.push_back(*selected);
					target->removeUnitBonus(remove);
				}
				else if(key.kind == Kind::BLOODLUST) target->updateUnitBonus(effect.incoming);
				else if(!newHorizonsStun::isStunned(*target)) target->addUnitBonus({newHorizonsStun::marker()});
			}
			DamageCache outcomeCache;
			float value = armyPressure(probe, reference, outcomeCache) - baselinePressure;
			for(const auto id : recipients)
			{
				const auto * before = board->battleGetUnitByID(id);
				const auto * after = probe->battleGetUnitByID(id);
				const auto loss = before->getAvailableHealth() - after->getAvailableHealth();
				if(loss > 0)
					value += (board->battleGetOwner(before) == reference ? -1 : 1)
						* AttackPossibility::calculateDamageReduce(nullptr, before, loss, cache, board);
			}
			expected += mass * value;
		}
	// Recipient marginal probabilities are exact for the observed uniform pools.
	// Value remains a sum of single-recipient stat deltas, without joint-status
	// or cross-recipient synergy and without a new outcome/search tree.
	for(const auto & [key, marginal] : bloodlust)
	{
		if(!recipients.contains(key.target)) continue;
		for(const auto id : recipients)
			probe->stackStates[id] = std::make_shared<StackWithBonuses>(probe.get(), board->battleGetUnitByID(id));
		++probe->bonusTreeVersion;
		probe->getForUpdate(key.target)->updateUnitBonus(marginal.second.incoming);
		DamageCache outcomeCache;
		expected += marginal.first * (armyPressure(probe, reference, outcomeCache) - baselinePressure);
	}
	if(expected >= 0) preview.defenderDamageReduce += static_cast<float>(expected);
	else preview.attackerDamageReduce -= static_cast<float>(expected);
	ledgers.clear(); // A repeated completion never credits the same opportunities twice.
	bloodlust.clear();
}
}
