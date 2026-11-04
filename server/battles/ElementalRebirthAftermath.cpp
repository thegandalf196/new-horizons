/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ElementalRebirthAftermath.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <vstd/RNG.h>
#include "../../lib/battle/NewHorizonsFactionSpellDamage.h"
namespace elementalRebirthAftermath
{
namespace
{
void primalBurst(CGameHandler & game, BattleID battleID, uint32_t sourceId)
{
	const auto * updatedBattle = game.gameState().getBattle(battleID);
	const auto * burstSource = updatedBattle
		? updatedBattle->battleGetUnitByID(sourceId) : nullptr;
	if(!burstSource || !burstSource->alive())
		return;

	const auto targetIds = newHorizonsElementalRebirth::adjacentHostileUnitIds(*updatedBattle, *burstSource);
	const auto damageBudget = newHorizonsElementalRebirth::primalBurstDamageBudget(
		burstSource->getAvailableHealth());
	const auto damagePerTarget = newHorizonsElementalRebirth::primalBurstShare(
		damageBudget, targetIds.size());
	if(damagePerTarget <= 0)
		return;

	StacksInjured burst;
	burst.battleID = battleID;
	BattleLogMessage burstLog;
	burstLog.battleID = battleID;
	for(const auto targetId : targetIds)
	{
		const auto * target = updatedBattle->battleGetUnitByID(targetId);
		if(!target || !target->alive() || !target->unitType())
			continue;

		const auto rawDamage = newHorizonsMagicalAbilityDamage::adjustDamage(
			*updatedBattle, *target, damagePerTarget);
		if(rawDamage <= 0)
			continue;

		BattleStackAttacked hit;
		hit.stackAttacked = targetId;
		hit.attackerID = sourceId;
		hit.damageAmount = rawDamage;
		const auto targetCreature = target->creatureId();
		const auto targetCount = target->getCount();
		CStack::prepareAttacked(hit, game.getRandomGenerator(), target->acquireState(),
			false, false, battle::DamageProvenance::SPELL);

		MetaString damageLine = MetaString::createFromRawString("Primal Burst deals ");
		damageLine.appendNumber(hit.damageAmount);
		damageLine.appendRawString(" elemental damage to ");
		damageLine.appendNumber(targetCount);
		damageLine.appendRawString(" ");
		damageLine.appendName(targetCreature, targetCount);
		damageLine.appendRawString(".");
		burstLog.lines.push_back(std::move(damageLine));
		burst.stacks.push_back(std::move(hit));
	}

	if(!burst.stacks.empty())
	{
		// Publish the damage text before the single authoritative injury packet.
		game.sendAndApply(burstLog);
		game.sendAndApply(burst);
	}
}
void decorate(CGameHandler & game, BattleID battleID, const newHorizonsElementalRebirth::DeathSnapshot & snapshot,
	const newHorizonsElementalRebirth::SpawnDescriptor & spawn, const battle::Unit & reborn, int64_t actualFullHealth)
{
	SetStackEffect provenance;
	provenance.battleID = battleID;
	provenance.toAdd.emplace_back(spawn.unit.id, std::vector<Bonus>{newHorizonsFactionSpellDamage::rebirthOutputMarker()});
	game.sendAndApply(provenance);

	if(const auto ward = newHorizonsElementalRebirth::elementalWardBonus(snapshot.profile))
	{
		SetStackEffect applyWard;
		applyWard.battleID = battleID;
		applyWard.toAdd.emplace_back(spawn.unit.id, std::vector<Bonus>{*ward});
		game.sendAndApply(applyWard);
	}

	const auto actualWound = actualFullHealth - spawn.health.targetAggregateHP;
	if(actualWound > 0)
	{
		auto state = reborn.acquireState();
		int64_t wound = actualWound;
		state->damage(wound);
		if(state->getAvailableHealth() != spawn.health.targetAggregateHP)
		{
			if(snapshot.chain)
			{
				BattleUnitsChanged rollback;
				rollback.battleID = battleID;
				rollback.changedStacks.emplace_back(spawn.unit.id, UnitChanges::EOperation::REMOVE);
				rollback.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
					snapshot.side, snapshot.unitId, spawn.unit.id, true};
				game.sendAndApply(rollback);
			}
			throw std::runtime_error("Elemental Rebirth failed to apply its exact aggregate HP target");
		}

		UnitChanges update(spawn.unit.id, UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -wound;
		BattleUnitsChanged damaged;
		damaged.battleID = battleID;
		damaged.changedStacks.push_back(std::move(update));
		game.sendAndApply(damaged);
	}

	if(snapshot.profile.swiftRebirth)
	{
		SetStackEffect swift;
		swift.battleID = battleID;
		swift.toAdd.emplace_back(spawn.unit.id, std::vector<Bonus>{
			newHorizonsSwiftRebirth::marker(game.gameState().getBattle(battleID)->getRound())});
		game.sendAndApply(swift);
	}
	const auto memory = newHorizonsElementalRebirth::elementalMemoryBonuses(snapshot, reborn);
	if(!memory.empty())
	{
		SetStackEffect inherited;
		inherited.battleID = battleID;
		inherited.toAdd.emplace_back(spawn.unit.id, memory);
		game.sendAndApply(inherited);
	}

}
}
void resolve(CGameHandler & game, uint32_t unitId, const Trigger & trigger)
{
	auto snapshot = trigger.snapshot;
	const auto * battleInfo = game.gameState().getBattle(trigger.battleID);
	const auto * unit = battleInfo ? battleInfo->battleGetUnitByID(unitId) : nullptr;
	if(!battleInfo || !newHorizonsElementalRebirth::stillEligibleDeath(unit, snapshot,
		true, false, false, battleInfo->getRebirthChainUsed(snapshot.side)))
		return;

	// First qualifying destruction spends Spark even if the Phoenix footprint
	// is blocked. Other deaths in this batch retain ordinary Rebirth.
	snapshot.phoenixSpark = snapshot.phoenixSpark && !battleInfo->getPhoenixSparkUsed(snapshot.side);
	if(snapshot.phoenixSpark)
	{
		BattleUnitsChanged receipt;
		receipt.battleID = trigger.battleID;
		receipt.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{snapshot.side, unitId};
		game.sendAndApply(receipt);
	}
	const auto candidates = newHorizonsElementalRebirth::legalCandidatePool(
		*battleInfo, battleInfo->getAccessibility(), snapshot);
	if(candidates.empty())
	{
		logGlobal->warn("Elemental Rebirth has no legal Elite Elemental for corpse hex %d",
			snapshot.corpsePosition.toInt());
		return;
	}

	const CreatureID creature = candidates.size() == 1 ? candidates.front()
		: *RandomGeneratorUtil::nextItem(candidates, game.getRandomGenerator());
	const auto desiredHP = newHorizonsElementalRebirth::targetHP(snapshot, creature, *battleInfo);
	const auto effectiveMaxHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
		battleInfo->getSideArmy(snapshot.side), creature,
		battleInfo->getSidePlayer(snapshot.side), snapshot.side);
	auto spawn = newHorizonsElementalRebirth::makeSpawnDescriptor(
		battleInfo->battleNextUnitId(), creature, snapshot.side, snapshot.corpsePosition,
		desiredHP, effectiveMaxHP, snapshot.chain);
	if(!spawn)
	{
		logGlobal->error("Elemental Rebirth could not represent %lld HP for creature id %d",
			static_cast<long long>(desiredHP), creature.getNum());
		return;
	}

	BattleUnitsChanged add;
	add.battleID = trigger.battleID;
	auto & addedUnit = add.changedStacks.emplace_back(spawn->unit.id, UnitChanges::EOperation::ADD);
	spawn->unit.save(addedUnit.data);
	if(snapshot.chain)
		add.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
			snapshot.side, snapshot.unitId, spawn->unit.id};
	game.sendAndApply(add);

	const auto * reborn = game.gameState().getBattle(trigger.battleID)->battleGetUnitByID(spawn->unit.id);
	if(!reborn)
		throw std::runtime_error("Elemental Rebirth stack was not published");
	const auto actualFullHealth = reborn->getAvailableHealth();
	if(actualFullHealth < spawn->health.targetAggregateHP)
	{
		logGlobal->error("Elemental Rebirth spawned %lld HP below its %lld HP target",
			static_cast<long long>(actualFullHealth), static_cast<long long>(spawn->health.targetAggregateHP));
		BattleUnitsChanged remove;
		remove.battleID = trigger.battleID;
		remove.changedStacks.emplace_back(spawn->unit.id, UnitChanges::EOperation::REMOVE);
		if(snapshot.chain)
			remove.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
				snapshot.side, snapshot.unitId, spawn->unit.id, true};
		game.sendAndApply(remove);
		return;
	}

	decorate(game, trigger.battleID, snapshot, *spawn, *reborn, actualFullHealth);

	BattleLogMessage log;
	log.battleID = trigger.battleID;
	MetaString line = MetaString::createFromRawString(snapshot.phoenixSpark ? "Phoenix Spark summons "
		: snapshot.chain ? "Rebirth Chain summons " : "Elemental Rebirth summons ");
	line.appendNumber(spawn->health.count);
	line.appendRawString(" ");
	line.appendName(creature, spawn->health.count);
	line.appendRawString(" with ");
	line.appendNumber(spawn->health.targetAggregateHP);
	line.appendRawString(" HP at the fallen stack's position.");
	if(snapshot.profile.greaterEssence && !snapshot.phoenixSpark)
		line.appendRawString(" Greater Essence increases its rebirth health.");
	if(snapshot.profile.elementalWard)
		line.appendRawString(" Elemental Ward protects it from magical damage.");
	log.lines.push_back(std::move(line));
	game.sendAndApply(log);

	if(snapshot.profile.primalBurst)
		primalBurst(game, trigger.battleID, spawn->unit.id);

}
}
