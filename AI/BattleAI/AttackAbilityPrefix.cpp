/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackAbilityPrefix.h"
#include "NominalAbilityChance.h"
#include "../../lib/battle/AttackAbilityQueries.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/spells/AbilityCaster.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
namespace attackAbilityPrefix
{
Query query(const CBattleInfoCallback & battle, const battle::Unit & source, const battle::Unit & target, bool ranged)
{
	Query result;
	const auto victimSide = battle.playerToSide(battle.battleGetOwner(&target));
	const auto policy = battle.battleGetVisibleHeroPolicy(victimSide);
	result.adverseFactVisible = policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO
		|| policy.kind() == VisibleHeroPolicy::Knowledge::KNOWN_ABSENT;
	if(result.adverseFactVisible)
		result.initialNominalRerollAvailability = battle.getBattle()->getAdverseCombatRerollState(victimSide).available() ? 1 : 0;
	result.nominalRerollAvailability = result.initialNominalRerollAvailability;
	if(!source.alive() || !target.alive() || !source.hasBonusOfType(BonusType::SPELL_AFTER_ATTACK))
		return result;
	const auto abilityBonuses = source.getBonuses(Selector::type()(BonusType::SPELL_AFTER_ATTACK));
	for(const auto id : attackAbilityQueries::spellsForAttackCasting(abilityBonuses, target))
	{
		const auto ability = attackAbilityQueries::parameters(source, BonusType::SPELL_AFTER_ATTACK, id, ranged);
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
		if(!spell->isNegative() || !hostile)
			continue;
		result.eligibleHarmfulAttempts.push_back(id);
		result.nominalRerollAvailability = nominalAbilityChance::availableAfter(
			creatureAbilityChance::rounding(source, ability.chance), result.nominalRerollAvailability);
	}
	return result;
}
}
