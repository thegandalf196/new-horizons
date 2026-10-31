/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/AttackAbilityQueries.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/spells/AbilityCaster.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/battle/CBattleInfoCallback.h"
void BattleActionProcessor::attackCasting(const CBattleInfoCallback & battle, bool ranged, BonusType attackMode, const battle::Unit * attacker, const CStack * defender, SpellID onlySpell)
{
	ObjectInstanceID ownerArmy = battle.getBattle()->getSideArmy(attacker->unitSide())->id;

	if(attacker->hasBonusOfType(attackMode))
	{
		TConstBonusListPtr spells = attacker->getBonuses(Selector::type()(attackMode));
		std::set<SpellID> spellsToCast = getSpellsForAttackCasting(spells, defender);

		for(SpellID spellID : spellsToCast)
		{
			if(onlySpell.hasValue() && spellID != onlySpell)
				continue;
			bool castMe = false;
			if(!defender->alive())
			{
				logGlobal->debug("attackCasting: all attacked creatures have been killed");
				return;
			}
			const auto ability = attackAbilityQueries::parameters(*attacker, attackMode, spellID, ranged);
			const int32_t spellLevel = ability.spellLevel;
			const int chance = ability.chance;
			castMe = ability.castMe;

			const CSpell * spell = SpellID(spellID).toSpell();
			spells::AbilityCaster caster(attacker, spellLevel);

			spells::Target target;
			target.emplace_back(defender);

			spells::BattleCast parameters(&battle, &caster, spells::Mode::PASSIVE, spell);

			auto m = spell->battleMechanics(&parameters);

			if(!m->canBeCastAt(target))
				continue;

			const auto attackerSide = battle.playerToSide(battle.battleGetOwner(attacker));
			const auto defenderSide = battle.playerToSide(battle.battleGetOwner(defender));
			const bool hostileTarget = (attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER)
				&& (defenderSide == BattleSide::ATTACKER || defenderSide == BattleSide::DEFENDER)
				&& defenderSide != attackerSide;
			bool procSucceeded = false;
			if(castMe && spell->isNegative() && hostileTarget)
			{
				const int effectiveChance = gameHandler->randomizer->prepareFavorableCreatureAbilityChance(*attacker, chance);
				const auto drawHostileSpell = [this, ownerArmy, effectiveChance]()
				{
					return gameHandler->randomizer->rollCombatAbility(ownerArmy, effectiveChance);
				};
				procSucceeded = owner->resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), defenderSide,
					effectiveChance > 0 && effectiveChance < 100, true, drawHostileSpell);
			}
			else
			{
				// Keep the original draw for positive, neutral, ineligible, or friendly-target spells.
				procSucceeded = gameHandler->randomizer->rollFavorableCreatureAbility(ownerArmy, *attacker, chance);
			}
			if(!procSucceeded)
				continue;

			//casting
			if(castMe)
			{
				parameters.cast(gameHandler->spellcastEnvironment(), target);
			}
		}
	}
}

std::set<SpellID> BattleActionProcessor::getSpellsForAttackCasting(const TConstBonusListPtr & spells, const CStack * defender)
{
	return attackAbilityQueries::spellsForAttackCasting(spells, *defender);
}

/// Legacy spell-casting abilities only - combat event reactions run from makeAttack instead, and
/// deliberately without these gates.
void BattleActionProcessor::handleAfterAttackCasting(const CBattleInfoCallback & battle, const CStack * attacker, const CStack * defender, const CombatEventPayload & payload)
{
	if(!attacker->alive()) // can be already dead, e.g. from retaliation
		return;

	if(defender->alive())
		attackCasting(battle, payload.ranged, BonusType::SPELL_AFTER_ATTACK, attacker, defender);
}
