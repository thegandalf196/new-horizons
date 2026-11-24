/*
 * HeroOrderEffects.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroOrderEffects.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsDiscipline.h"
#include "NewHorizonsOffense.h"
#include "Unit.h"
#include "NewHorizonsWarcast.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/CSpellHandler.h"
#include "../mapObjects/CGHeroInstance.h"

namespace heroCommands
{
std::optional<PreparedOrderEffects> prepareOrderEffects(const CBattleInfoCallback & battle,
	BattleSide side, HeroCommand command, const std::vector<uint32_t> & targets)
{
	const auto order = battle.battlePrepareHeroOrderState(side, command, targets);
	if(!order)
		return {};
	PreparedOrderEffects result{side, *order, {}};
	if(command == HeroCommand::FOCUS_FIRE)
	{
		result.focusFire = battle.battlePrepareFocusFireState(side, order->primaryTargetUnitId);
		if(!result.focusFire)
			return {};
	}
	return result;
}

IssuedOrderGrants prepareIssuedOrderGrants(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	IssuedOrderGrants result;
	appendVanguardGrants(result, battle, side, order);
	appendSustainingGrants(result, battle, side, order);
	const auto * hero = battle.battleGetFightingHero(side);
	if(order.command == HeroCommand::RIPOSTE && hero
		&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE))
	{
		IssuedOrderGrants::Batch batch;
		for(const auto * unit : battle.battleGetAllUnits(true))
		{
			if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
				|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
				|| battle.battleGetOwner(unit) != battle.sideToPlayer(side)
				|| newHorizonsOffense::hasVengeanceRetaliationBonus(unit))
				continue;
			batch.emplace_back(unit->unitId(), std::vector<Bonus>{newHorizonsOffense::vengeanceRetaliationBonus()});
		}
		if(!batch.empty())
			result.batches.push_back(std::move(batch));
	}
	if(order.command == HeroCommand::HOLD_THE_LINE && newHorizonsDiscipline::hasHoldFast(hero))
	{
		IssuedOrderGrants::Batch batch;
		std::set<uint32_t> added;
		for(const auto & anchor : order.anchors)
		{
			if(!added.insert(anchor.unitId).second)
				continue;
			const auto * unit = battle.battleGetUnitByID(anchor.unitId);
			if(!unit || !unit->alive() || unit->isGhost()
				|| battle.battleGetOwner(unit) != battle.sideToPlayer(side)
				|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit))
				continue;
			++result.holdFastRecipientCount;
			if(!unit->hasBonus(newHorizonsDiscipline::holdFastMoraleFloorBonusSelector()))
				batch.emplace_back(unit->unitId(), std::vector<Bonus>{newHorizonsDiscipline::holdFastMoraleFloorBonus()});
		}
		if(!batch.empty())
			result.batches.push_back(std::move(batch));
	}
	return result;
}

void appendSustainingGrants(IssuedOrderGrants & result, const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	if(order.warcast.recipients.empty()
		|| !newHorizonsWarcast::hasPerk(battle.battleGetFightingHero(side), newHorizonsWarcast::SUSTAINING_COMMAND))
		return;
	const auto owner = battle.sideToPlayer(side);
	IssuedOrderGrants::Batch removals;
	IssuedOrderGrants::Batch additions;
	for(const auto & entry : order.warcast.recipients)
	{
		const auto * unit = battle.battleGetUnitByID(entry.unitId);
		if(!unit || !unit->alive())
			continue;
		std::vector<Bonus> expiring;
		std::vector<Bonus> extended;
		const auto effects = unit->getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
		for(const auto & effect : *effects)
		{
			if(!Bonus::NTurns(effect.get()) || effect->turnsRemain != 1
				|| effect->spellCasterOwner != owner || effect->abilityDescriptor)
				continue;
			const auto spell = effect->sid.as<SpellID>();
			if(!spell.hasValue() || !spell.toSpell()->isPositive())
				continue;
			const auto marker = newHorizonsWarcast::sustainedMarker(*effect);
			if(unit->hasBonus(CSelector([&marker](const Bonus * bonus)
				{ return newHorizonsWarcast::isSustainedMarker(bonus) && bonus->stacking == marker.stacking; }))
				|| std::any_of(extended.begin(), extended.end(), [&](const Bonus & bonus)
				{ return bonus.stacking == marker.stacking; }))
				continue;
			// Every component of this spell on the stack expires together and is extended together.
			for(const auto & component : *effects)
				if(component->sid == effect->sid && Bonus::NTurns(component.get())
					&& component->turnsRemain == 1 && component->spellCasterOwner == owner)
				{
					expiring.push_back(*component);
					auto copy = *component;
					copy.turnsRemain = 2;
					extended.push_back(copy);
				}
			auto lasting = marker;
			lasting.turnsRemain = 2;
			extended.push_back(lasting);
			++result.sustainedEffectCount;
		}
		if(!expiring.empty())
		{
			removals.emplace_back(entry.unitId, std::move(expiring));
			additions.emplace_back(entry.unitId, std::move(extended));
		}
	}
	if(!removals.empty())
	{
		result.removals.push_back(std::move(removals));
		result.batches.push_back(std::move(additions));
	}
}

void applyIssuedOrderGrants(IBattleState & state, const IssuedOrderGrants & grants)
{
	for(const auto & batch : grants.removals)
		for(const auto & [id, bonuses] : batch)
			state.removeUnitBonus(id, bonuses);
	for(const auto & batch : grants.batches)
		for(const auto & [id, bonuses] : batch)
			state.addUnitBonus(id, bonuses);
}
}
