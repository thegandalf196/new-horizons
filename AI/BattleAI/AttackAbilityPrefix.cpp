/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackAbilityPrefix.h"
#include "NominalAbilityChance.h"
#include "../../lib/battle/AttackAbilityQueries.h"
#include "../../lib/battle/NewHorizonsSpecialtyProcs.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/spells/AbilityCaster.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
namespace attackAbilityPrefix
{
bool alreadyApplied(const std::set<StatusKey> & applied, const StatusKey & key)
{
	if(key.spell == SpellID::DISPEL_HELPFUL_SPELLS)
		return !key.records.empty() && std::ranges::all_of(key.records, [&](const Bonus * record)
		{
			return std::ranges::any_of(applied, [&](const StatusKey & previous)
			{
				return previous.target == key.target && previous.spell == key.spell
					&& std::ranges::find(previous.records, record) != previous.records.end();
			});
		});
	return applied.contains(key);
}

Query query(const CBattleInfoCallback & battle, const battle::Unit & source, const battle::Unit & target, bool ranged, bool specialtyStrike)
{
	Query result;
	const auto victimSide = battle.playerToSide(battle.battleGetOwner(&target));
	const auto policy = battle.battleGetVisibleHeroPolicy(victimSide);
	result.adverseFactVisible = policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO
		|| policy.kind() == VisibleHeroPolicy::Knowledge::KNOWN_ABSENT;
	if(result.adverseFactVisible)
		result.initialNominalRerollAvailability = battle.getBattle()->getAdverseCombatRerollState(victimSide).available() ? 1 : 0;
	result.nominalRerollAvailability = result.initialNominalRerollAvailability;
	if(!source.alive() || !target.alive())
		return result;
	const auto abilityBonuses = source.getBonuses(Selector::type()(BonusType::SPELL_AFTER_ATTACK));
	auto selected = attackAbilityQueries::spellsForAttackCasting(abilityBonuses, target);
	for(const auto id : newHorizonsSpecialtyProcs::additionalAttackSpells(battle, source, ranged))
		if(specialtyStrike || id == SpellID::POISON) selected.insert(id);
	for(const auto id : selected)
	{
		auto ability = attackAbilityQueries::parameters(source, BonusType::SPELL_AFTER_ATTACK, id, ranged);
		const auto chance = specialtyStrike || id == SpellID::POISON
			? newHorizonsSpecialtyProcs::attackSpellChance(battle, source, id, ranged) : std::optional<int>{};
		if(chance)
		{
			ability.castMe = true;
			ability.chance = *chance;
		}
		const auto * spell = id.toSpell();
		spells::AbilityCaster caster(&source, ability.spellLevel);
		spells::Target destinations; destinations.emplace_back(&target);
		spells::BattleCast parameters(&battle, &caster, spells::Mode::PASSIVE, spell);
		const auto mechanics = spell->battleMechanics(&parameters);
		if(!mechanics->canBeCastAt(destinations) || !ability.castMe)
			continue;
		// Paralysis changes action permission, not physical Poison's living-target eligibility.
		result.eligibilityStableForPoison &= id == SpellID::PARALYZE;
		const auto sourceSide = battle.playerToSide(battle.battleGetOwner(&source));
		const bool hostile = (sourceSide == BattleSide::ATTACKER || sourceSide == BattleSide::DEFENDER)
			&& (victimSide == BattleSide::ATTACKER || victimSide == BattleSide::DEFENDER) && sourceSide != victimSide;
		const bool harmful = spell->isNegative() && hostile;
		std::optional<StatusKey> condition;
		if(chance && id == SpellID::DISPEL_HELPFUL_SPELLS)
		{
			StatusKey selected{target.unitId(), id, {}};
			const auto bonuses = target.getBonuses(Selector::all, "all");
			for(const auto & bonus : *bonuses)
				if(bonus && newHorizonsSpecialtyProcs::isPositiveTemporaryMagic(*bonus)) selected.records.push_back(bonus.get());
			if(!selected.records.empty()) condition = std::move(selected);
		}
		const auto rounding = creatureAbilityChance::rounding(source, ability.chance);
		result.attempts.push_back({id, ability.chance, nominalAbilityChance::success(rounding,
			harmful ? result.nominalRerollAvailability : 0), harmful,
			chance.has_value() || (id == SpellID::POISON && newHorizonsSpecialtyProcs::poisonPotencyMultiplier(battle, source) > 1),
			mechanics->getEffectDuration(), std::move(condition)});
		if(!harmful) continue;
		result.eligibleHarmfulAttempts.push_back(id);
		result.nominalRerollAvailability = nominalAbilityChance::availableAfter(rounding, result.nominalRerollAvailability);
	}
	const int stunChance = specialtyStrike
		? newHorizonsSpecialtyProcs::stunChance(battle, BattleAttackInfo(&source, &target, 0, ranged)) : 0;
	if(stunChance > 0)
	{
		const auto rounding = creatureAbilityChance::rounding(source, stunChance);
		result.stunChancePercent = stunChance;
		result.stunCondition = StatusKey{target.unitId(), SpellID::NONE, {}};
		result.stunSuccess = nominalAbilityChance::success(rounding, result.nominalRerollAvailability);
		result.nominalRerollAvailability = nominalAbilityChance::availableAfter(rounding, result.nominalRerollAvailability);
	}
	return result;
}
}
