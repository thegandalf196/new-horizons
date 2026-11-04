/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ElementalCataclysmAftermath.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace elementalCataclysmAftermath
{
namespace
{
void captureHits(Triggers & result, const CGameHandler & game, BattleID battleID,
	const std::vector<BattleStackAttacked> & hits)
{
	const auto * battle = game.gameState().getBattle(battleID);
	if(!battle) return;
	for(const auto & hit : hits)
	{
		if(!hit.killed() || hit.cloneKilled() || hit.willRebirth()) continue;
		const auto * unit = battle->battleGetUnitByID(hit.stackAttacked);
		if(!unit) continue;
		if(auto snapshot = newHorizonsFactionSpellDamage::captureCataclysm(*battle, *unit))
			result.try_emplace(unit->unitId(), Trigger{battleID, std::move(*snapshot)});
	}
}
void explode(CGameHandler & game, const Trigger & trigger)
{
	const auto * battle = game.gameState().getBattle(trigger.battleID);
	const auto & captured = trigger.captured;
	const auto * corpse = battle ? battle->battleGetUnitByID(captured.source) : nullptr;
	if(!corpse || corpse->alive() || corpse->isClone() || corpse->unitSide() != captured.side) return;
	const auto share = newHorizonsFactionSpellDamage::cataclysmShare(
		captured.maximumAggregateHP, captured.targets.size());
	if(share <= 0) return;
	StacksInjured explosion;
	explosion.battleID = trigger.battleID;
	BattleLogMessage log;
	log.battleID = trigger.battleID;
	for(const auto id : captured.targets)
	{
		const auto * target = battle->battleGetUnitByID(id);
		if(!target || !target->alive() || !target->unitType()) continue;
		const auto damage = newHorizonsFactionSpellDamage::cataclysmDamage(*battle, *target, share);
		if(damage <= 0) continue;
		BattleStackAttacked hit;
		hit.stackAttacked = id;
		hit.attackerID = captured.source;
		hit.damageAmount = damage;
		CStack::prepareAttacked(hit, game.getRandomGenerator(), target->acquireState(),
			false, false, battle::DamageProvenance::SPELL);
		MetaString line = MetaString::createFromRawString("Elemental Cataclysm deals ");
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" magical damage to ");
		line.appendName(target->creatureId(), target->getCount());
		line.appendRawString(".");
		log.lines.push_back(std::move(line));
		explosion.stacks.push_back(std::move(hit));
	}
	if(!explosion.stacks.empty())
	{
		game.sendAndApply(log);
		game.sendAndApply(explosion);
	}
}
}
Triggers capture(const CGameHandler & game, const CPackForClient & packet)
{
	Triggers result;
	if(const auto * attack = dynamic_cast<const BattleAttack *>(&packet))
		captureHits(result, game, attack->battleID, attack->bsa);
	else if(const auto * injured = dynamic_cast<const StacksInjured *>(&packet))
		captureHits(result, game, injured->battleID, injured->stacks);
	return result;
}
void resolve(CGameHandler & game, const Triggers & triggers)
{
	for(const auto & [id, trigger] : triggers) explode(game, trigger);
}
}
