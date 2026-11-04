/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandateSpellPool.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/CurePhysicalAffliction.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include "../spells/NewHorizonsTimedUtility.h"
#include "../spells/NewHorizonsPurify.h"
#include <algorithm>

namespace newHorizonsPassiveDivineMandate
{
BattleAction automaticSpellAction(const CBattleInfoCallback & battle,
	BattleSide side, SpellID spell, uint32_t aimUnitId)
{
	BattleAction action;
	action.side = side;
	action.actionType = EActionType::HERO_SPELL;
	action.stackNumber = side == BattleSide::ATTACKER ? -1 : -2;
	action.spell = spell;
	const auto * target = battle.battleGetUnitByID(aimUnitId);
	if(!target)
		return action;
	action.setTarget({battle::Destination(target)});
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero)
		return action;
	const auto & rules = battle.getBattle()->getMagicRules();
	if(spell == SpellID::CURE)
	{
		const auto afflictions = newHorizonsMagic::cureAfflictions(rules, target);
		if(!afflictions.empty())
			action.spellCureAffliction = afflictions.front();
		else if(spells::curePhysical::present(rules, *target, "frozen"))
			action.spellCurePhysicalAffliction = "frozen";
		else if(spells::curePhysical::present(rules, *target, "graveDust"))
			action.spellCurePhysicalAffliction = "graveDust";
	}
	if(spell == newHorizonsPurify::spellID())
	{
		action.setTarget({battle::Destination(target->getPosition())});
		for(const auto & row : newHorizonsPurify::eligibleStacks(battle, side, target->getPosition(),
			hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), newHorizonsPurify::hasPurifierPerk(hero)))
		{
			int remaining = row.maximumSpellEffectChoices;
			for(const auto group : row.spellEffectGroups)
				if(remaining-- > 0)
					action.spellPurifyChoices.emplace_back(row.unitId, group);
			if(remaining > 0 && row.physicalPoison && !row.physicalPoisonAutomaticallyCleared)
				action.spellPurifyChoices.emplace_back(row.unitId, newHorizonsPurify::physicalPoisonChoiceID());
		}
	}
	return action;
}

namespace
{
bool usefulRecipient(const CBattleInfoCallback & battle, const CSpell & spell,
	const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	if(spell.getId() == SpellID::CURE)
		return true; // Native Cure admission verifies wounds or selected affliction.
	const auto family = newHorizonsMagic::spellVariantBase(battle.getBattle()->getMagicRules(), spell.getId());
	const auto current = unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(family)));
	if(const auto exact = newHorizonsMagic::usefulTimedLightProtection(mechanics, unit))
		return *exact;
	// Retain the explicit native duration contracts for these historical authored
	// families. Per-recipient scaling reads pre-cast marks, never newly published marks.
	const auto key = family.toSpell()->getJsonKey();
	if(key == "new-horizons:theChosen" || key == "new-horizons:crusade"
		|| !mechanics.usesNewHorizonsMagicV3())
		return current->empty() || std::ranges::any_of(*current, [&](const auto & bonus)
		{
			return bonus->turnsRemain < mechanics.adjustMetamagicDuration(
				newHorizonsMagic::timedLightEffectDuration(mechanics), &unit);
		});
	// Unknown/custom producer semantics: admit an absent effect, but never spend
	// a free activation on an existing effect using a guessed generic SP timer.
	return current->empty();
}
}

std::vector<SpellChoice> buildSpellPool(const CBattleInfoCallback & battle,
	const CGHeroInstance & hero, spells::AutomaticHeroCastReceipt receipt,
	const std::vector<uint32_t> & orderRecipientUnitIds)
{
	std::vector<SpellChoice> choices;
	const auto side = battle.playerToSide(hero.getCasterOwner());
	for(const auto id : hero.getSpellsInSpellbook())
	{
		const auto * spell = id.toSpell();
		if(!spells::automaticHeroCastAllowed(battle, &hero, spell, receipt))
			continue;
		SpellChoice row;
		row.spell = id;
		row.family = newHorizonsMagic::spellVariantBase(battle.getBattle()->getMagicRules(), id);
		for(const auto * aim : battle.battleGetAllUnits(false))
		{
			if(!aim->alive() || aim->isGhost() || battle.battleGetOwner(aim) != hero.getCasterOwner())
				continue;
			const auto action = automaticSpellAction(battle, side, id, aim->unitId());
			spells::BattleCast cast(&battle, &hero, spells::Mode::HERO, spell);
			cast.setAutomaticHeroCast(receipt);
			cast.setCureAffliction(action.spellCureAffliction);
			cast.setCurePhysicalAffliction(action.spellCurePhysicalAffliction);
			cast.setPurifyChoices(action.spellPurifyChoices);
			const auto target = action.getTarget(&battle);
			const auto mechanics = cast.mechanicsForTarget(target);
			if(!mechanics->canBeCastAt(target))
				continue;
			row.massive = mechanics->isMassive();
			TargetChoice option;
			option.aimUnitId = aim->unitId();
			bool useful = false;
			if(id == newHorizonsPurify::spellID())
			{
				option.recipientUnitIds = newHorizonsPurify::prospectiveRecipients(battle, side,
					aim->getPosition(), hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER),
					newHorizonsPurify::hasPurifierPerk(&hero), action.spellPurifyChoices);
				for(const auto unitId : option.recipientUnitIds)
				{
					const auto * unit = battle.battleGetUnitByID(unitId);
					if(!unit)
						continue;
					const bool returnsEnemy = std::ranges::any_of(action.spellPurifyChoices,
						[&](const auto & choice)
						{
							return choice.first == static_cast<int32_t>(unitId)
								&& newHorizonsPurify::removalReturnsHostileStack(*unit, side, choice.second);
						});
					// Removing other ailments from the same released enemy is no friendly benefit.
					useful |= !returnsEnemy;
				}
			}
			else
				for(const auto * unit : mechanics->getAffectedStacks(target))
					if(unit && battle.battleGetOwner(unit) == hero.getCasterOwner())
					{
						option.recipientUnitIds.push_back(unit->unitId());
						useful |= usefulRecipient(battle, *spell, *mechanics, *unit);
					}
			if(!useful || option.recipientUnitIds.empty())
				continue;
			std::sort(option.recipientUnitIds.begin(), option.recipientUnitIds.end());
			option.recipientUnitIds.erase(std::unique(option.recipientUnitIds.begin(), option.recipientUnitIds.end()),
				option.recipientUnitIds.end());
			row.targets.push_back(std::move(option));
			if(row.massive)
				break; // One global aim, never an extra random target draw.
		}
		if(!row.targets.empty())
			choices.push_back(std::move(row));
	}
	return finalizeSpellPool(std::move(choices), orderRecipientUnitIds);
}
}
