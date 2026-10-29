/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
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
			int32_t spellLevel = 0;
			TConstBonusListPtr spellsByType = attacker->getBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
			for(const auto & sf : *spellsByType)
			{
				int meleeRanged = -1;
				if (sf->parameters)
				{
					vstd::amax(spellLevel, sf->parameters->toVector()[0]);
					meleeRanged = sf->parameters->toVector()[1];
				}

				if (meleeRanged == -1 || meleeRanged == 0 || (meleeRanged == 1 && ranged) || (meleeRanged == 2 && !ranged))
					castMe = true;
			}
			int chance = attacker->valOfBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
			vstd::amin(chance, 100);

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

std::set<SpellID> BattleActionProcessor::getSpellsForAttackCasting(const TConstBonusListPtr & spells, const CStack *defender)
{
	std::set<SpellID> spellsToCast;
	constexpr int unlayeredItemsInternalLayer = -1;

	std::map<int, std::vector<std::shared_ptr<Bonus>>> spellsWithBackupLayers;

	for(int i = 0; i < spells->size(); i++)
	{
		std::shared_ptr<Bonus> bonus = spells->operator[](i);
		int layer = bonus->parameters ? bonus->parameters->toVector()[2] : -1;
		vstd::amax(layer, -1);
		spellsWithBackupLayers[layer].push_back(bonus);
	}

	auto addSpellsFromLayer = [&](int layer) -> void
	{
		assert(spellsWithBackupLayers.find(layer) != spellsWithBackupLayers.end());

		for(const auto & spell : spellsWithBackupLayers[layer])
		{
			if (spell->subtype.as<SpellID>() != SpellID())
				spellsToCast.insert(spell->subtype.as<SpellID>());
			else
				logGlobal->error("Invalid spell to cast during attack!");
		}
	};

	if(spellsWithBackupLayers.find(unlayeredItemsInternalLayer) != spellsWithBackupLayers.end())
	{
		addSpellsFromLayer(unlayeredItemsInternalLayer);
		spellsWithBackupLayers.erase(unlayeredItemsInternalLayer);
	}

	for(auto item : spellsWithBackupLayers)
	{
		bool areCurrentLayerSpellsApplied = std::all_of(item.second.begin(), item.second.end(),
			[&](const std::shared_ptr<Bonus> spell)
			{
				std::vector<SpellID> activeSpells = defender->activeSpells();
				return vstd::find(activeSpells, spell->subtype.as<SpellID>()) != activeSpells.end();
			});

		if(!areCurrentLayerSpellsApplied || item.first == spellsWithBackupLayers.rbegin()->first)
		{
			addSpellsFromLayer(item.first);
			break;
		}
	}

	return spellsToCast;
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
