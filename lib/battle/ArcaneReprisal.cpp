/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneReprisal.h"
#include "ArcaneInspiration.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsMagicalAbilityDamage.h"
#include "Unit.h"
#include "../CStack.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>

namespace arcaneInspiration
{
std::optional<Reprisal> captureReprisal(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t inflictedDamage)
{
	if(!attack.attacker || !attack.defender || inflictedDamage <= 0
		|| attack.defender->isInvincible() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.defender)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return {};
	const auto policy = activeProfile(*attack.defender, battle.battleGetOwner(attack.defender));
	if(policy.corePercent == 0 || !(policy.riders & REPRISAL))
		return {};
	const auto amount = reprisalRawDamage(inflictedDamage);
	return amount > 0 ? std::optional(Reprisal{attack.defender->unitId(), attack.attacker->unitId(), amount}) : std::nullopt;
}
int64_t reprisalDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage)
{
	if(target.isInvincible() || !target.alive() || target.isGhost())
		return 0;
	return newHorizonsMagicalAbilityDamage::adjustDamage(battle, target, rawDamage);
}
void publishReprisals(ServerCallback & server, const CBattleInfoCallback & battle, std::span<const Reprisal> reprisals)
{
	for(const auto & receipt : reprisals)
	{
		const auto * target = battle.battleGetUnitByID(receipt.target);
		if(!target)
			continue;
		const auto amount = reprisalDamage(battle, *target, receipt.rawDamage);
		if(amount <= 0)
			continue;
		BattleStackAttacked hit;
		hit.stackAttacked = receipt.target;
		hit.attackerID = receipt.source;
		hit.damageAmount = amount;
		hit.flags = BattleStackAttacked::SECONDARY;
		auto state = target->acquireState();
		CStack::prepareAttacked(hit, *server.getRNG(), state, false, false, battle::DamageProvenance::SPELL);
		StacksInjured injury;
		injury.battleID = battle.getBattle()->getBattleID();
		injury.stacks.push_back(std::move(hit));
		server.apply(injury);
		if(server.describeChanges())
		{
			BattleLogMessage log;
			log.battleID = injury.battleID;
			MetaString line;
			line.appendRawString("Arcane Reprisal deals ");
			line.appendNumber(injury.stacks.front().damageAmount);
			line.appendRawString(" magical damage.");
			log.lines.push_back(std::move(line));
			server.apply(log);
		}
	}
}
}
