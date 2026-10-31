/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleCreatureBurn.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace battleCreatureBurn
{
bool onActivation(CGameHandler & game, const CBattleInfoCallback & battle, const CStack & target,
	BattleUnitTurnReason reason)
{
	const auto tick = newHorizonsCreatureBurn::activationTick(battle, target, reason);
	if(tick.remove.empty())
		return false;
	SetStackEffect consume;
	consume.battleID = battle.getBattle()->getBattleID();
	consume.toRemove.emplace_back(target.unitId(), tick.remove);
	game.sendAndApply(consume);
	if(tick.damage <= 0)
		return true;
	BattleStackAttacked hit;
	hit.attackerID = tick.source;
	hit.stackAttacked = target.unitId();
	hit.damageAmount = tick.damage;
	CStack::prepareAttacked(hit, game.getRandomGenerator(), target.acquireState(),
		false, false, battle::DamageProvenance::OTHER);
	StacksInjured injury;
	injury.battleID = battle.getBattle()->getBattleID();
	injury.stacks.push_back(hit);
	game.sendAndApply(injury);
	if(hit.damageAmount > 0)
	{
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString("Burn deals ");
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" Fire damage to %s.");
		target.addNameReplacement(line, target.getCount());
		message.lines.push_back(std::move(line));
		game.sendAndApply(message);
	}
	return true;
}
}
