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
#include "NewHorizonsEnchantedCommand.h"
#include "NewHorizonsOffense.h"
#include "Unit.h"
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
	const auto * hero = battle.battleGetFightingHero(side);
	if(newHorizonsEnchantedCommand::eligible(battle.getBattle()->getMagicRules(), hero, order))
	{
		IssuedOrderGrants::Batch batch;
		for(const auto id : newHorizonsEnchantedCommand::recipientIds(battle, side, order))
		{
			const auto * unit = battle.battleGetUnitByID(id);
			if(!unit->hasBonus(newHorizonsEnchantedCommand::moraleBonusSelector()))
				batch.emplace_back(id, std::vector<Bonus>{newHorizonsEnchantedCommand::moraleBonus()});
		}
		if(!batch.empty())
			result.batches.push_back(std::move(batch));
	}
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
