/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleFlowProcessor.h"
#include "../../lib/battle/DemonicGateArrival.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/IncomingMagicalDamage.h"

namespace
{
using newHorizonsDemonicGating::ArrivalPerks;
using newHorizonsDemonicGating::arrivalPerks;
using newHorizonsDemonicGating::beaconEligible;
bool due(const SideInBattle::PendingDemonicGate & gate, int round, bool endPhase, bool swift)
{
	return endPhase ? gate.chainGateAccelerated || gate.arrivalRound <= round
		|| (swift && gate.arrivalRound <= round + 1) : gate.arrivalRound <= round;
}
bool canArrive(const CBattleInfoCallback & battle, const SideInBattle::PendingDemonicGate & gate, BattleSide side)
{
	if(!gate.creature.hasValue() || gate.count <= 0)
		return false;
	const auto * creature = gate.creature.toCreature();
	return creature && battle.getAccessibility().accessibleForDemonicGateArrival(
		gate.position, creature->isDoubleWide(), side, gate.position, creature->isDoubleWide())
		&& !newHorizonsDemonicGating::footprintHasObstacle(battle, gate.position, creature->isDoubleWide(), side);
}
uint32_t addStack(CGameHandler & game, const BattleInfo & battle,
	const SideInBattle::PendingDemonicGate & gate, BattleSide side)
{
	battle::UnitInfo info;
	info.id = battle.battleNextUnitId();
	info.count = gate.count;
	info.type = gate.creature;
	info.side = side;
	info.position = gate.position;
	// Native summoned provenance owns all temporary casualty/reward exclusions.
	// Legacy reserve troops retain their original owned-army representation.
	info.summoned = gate.temporary;
	BattleUnitsChanged add;
	add.battleID = battle.getBattleID();
	add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(add.changedStacks.back().data);
	game.sendAndApply(add);
	const auto devil = CreatureID(CreatureID::decode("core:devil"));
	if(devil.hasValue() && devil.toCreature() && !devil.toCreature()->sounds.startMoving.empty())
	{
		BattleAnimationPlayed sound;
		sound.battleID = battle.getBattleID();
		sound.sound = devil.toCreature()->sounds.startMoving;
		sound.targets.push_back({static_cast<int32_t>(info.id), gate.position});
		game.sendAndApply(sound);
	}
	return info.id;
}
int64_t reinforce(CGameHandler & game, const CBattleInfoCallback & battle, uint32_t id)
{
	const auto * stack = battle.battleGetStackByID(id, false);
	if(!stack)
		return 0;
	auto state = stack->acquireState();
	const int64_t health = newHorizonsDemonicGating::reinforcedHitPoints(state->getAvailableHealth());
	state->health.addTemporaryHitPoints(health);
	BattleUnitsChanged update;
	update.battleID = battle.getBattle()->getBattleID();
	UnitChanges changed(id, UnitChanges::EOperation::UPDATE);
	changed.data = state->save();
	update.changedStacks.push_back(std::move(changed));
	game.sendAndApply(update);
	return health;
}
void grantArrivalBonuses(CGameHandler & game, const CBattleInfoCallback & battle, uint32_t id,
	const ArrivalPerks & perks, bool endPhase, bool beacon)
{
	auto bonuses = newHorizonsDemonicGating::arrivalBonuses(perks, endPhase, beacon);
	if(!bonuses.empty())
	{
		SetStackEffect effect;
		effect.battleID = battle.getBattle()->getBattleID();
		effect.toAdd.emplace_back(id, std::move(bonuses));
		game.sendAndApply(effect);
	}
}
void logArrival(CGameHandler & game, const BattleInfo & battle,
	const SideInBattle::PendingDemonicGate & gate, bool endPhase, int64_t health, bool beacon, const ArrivalPerks & perks)
{
	BattleLogMessage message;
	message.battleID = battle.getBattleID();
	MetaString line = MetaString::createFromRawString(gate.chainGateAccelerated ? "Chain Gate" : endPhase ? "Swift Gate" : "Ordinary Gate");
	line.appendRawString(" brings forth ");
	line.appendNumber(gate.count);
	line.appendRawString(" ");
	line.appendName(gate.creature, gate.count);
	line.appendRawString(".");
	if(health > 0)
	{
		line.appendRawString(" Reinforced Gate grants ");
		line.appendNumber(health);
		line.appendRawString(" temporary Health.");
	}
	if(beacon)
		line.appendRawString(" Infernal Beacon grants +2 Initiative.");
	if(perks.discipline)
		line.appendRawString(perks.temporary ? " Infernal Discipline prevents negative Morale." : " Reserve Discipline prevents negative Morale.");
	message.lines.push_back(std::move(line));
	game.sendAndApply(message);
}
void applyHellfire(CGameHandler & game, const CBattleInfoCallback & battle, BattleSide side, uint32_t source, bool temporary)
{
	const auto * gated = battle.battleGetStackByID(source, false);
	if(!gated)
		return;
	const auto enemies = newHorizonsDemonicGating::hellfireTargets(battle, *gated, side, temporary);
	const int64_t each = newHorizonsDemonicGating::hellfireRawPerEnemy(gated->getAvailableHealth(), enemies.size());
	if(each <= 0)
		return;
	StacksInjured injury;
	injury.battleID = battle.getBattle()->getBattleID();
	BattleLogMessage log;
	log.battleID = injury.battleID;
	for(const auto * enemy : enemies)
	{
		BattleStackAttacked hit;
		hit.attackerID = source;
		hit.stackAttacked = enemy->unitId();
		hit.damageAmount = newHorizonsDemonicGating::hellfireDamage(battle, *enemy, each, temporary);
		hit.flags |= BattleStackAttacked::SPELL_EFFECT;
		CStack::prepareAttacked(hit, game.getRandomGenerator(), enemy->acquireState(), false, false, battle::DamageProvenance::SPELL);
		MetaString line = MetaString::createFromRawString("Hellfire from ");
		line.appendNumber(gated->getCount());
		line.appendRawString(" ");
		line.appendName(gated->creatureId(), gated->getCount());
		line.appendRawString(" hits ");
		line.appendNumber(enemy->getCount());
		line.appendRawString(" ");
		line.appendName(enemy->creatureId(), enemy->getCount());
		line.appendRawString(" for ");
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" damage, killing ");
		line.appendNumber(hit.killedAmount);
		line.appendRawString(".");
		log.lines.push_back(std::move(line));
		injury.stacks.push_back(hit);
	}
	game.sendAndApply(injury);
	game.sendAndApply(log);
}
}

void BattleFlowProcessor::resolveDemonicGates(const CBattleInfoCallback & battle, bool endPhase)
{
	for(const auto sideId : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
		if(!concrete)
			continue;
		const auto snapshot = concrete->getSide(sideId);
		if(snapshot.pendingDemonicGates.empty())
			continue;
		const auto perks = arrivalPerks(battle.battleGetFightingHero(sideId));
		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = sideId;
		update.reserve = snapshot.demonicReserve;
		update.gated = snapshot.gatedDemonicStacks;
		update.chainGateArmed = snapshot.chainGateArmed;
		update.masterGateUsed = snapshot.masterGateUsed;
		std::vector<std::pair<uint32_t, bool>> hellfire;
		for(const auto & gate : snapshot.pendingDemonicGates)
		{
			if(!due(gate, concrete->getRound(), endPhase, perks.swift) || !canArrive(battle, gate, sideId))
			{
				update.pending.push_back(gate);
				continue;
			}
			const auto id = addStack(*gameHandler, *concrete, gate, sideId);
			update.gated.push_back({id, gate.creature, gate.count, gate.temporary});
			const auto health = perks.reinforced ? reinforce(*gameHandler, battle, id) : 0;
			const auto * stack = battle.battleGetStackByID(id, false);
			const bool beacon = perks.beacon && stack && beaconEligible(battle, *stack, sideId);
			grantArrivalBonuses(*gameHandler, battle, id, perks, endPhase, beacon);
			if(perks.hellfire)
				hellfire.emplace_back(id, gate.temporary);
			logArrival(*gameHandler, *concrete, gate, endPhase, health, beacon, perks);
		}
		// Publish identities before damage so a lethal Hellfire arms Chain Gate.
		gameHandler->sendAndApply(update);
		for(const auto & [source, temporary] : hellfire)
			applyHellfire(*gameHandler, battle, sideId, source, temporary);
	}
}
