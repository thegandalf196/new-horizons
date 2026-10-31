/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/CStack.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
battlePrimaryStrikeAftermath::Prepared BattleActionProcessor::preparePrimaryStrikeAftermath(
	const CBattleInfoCallback & battle, const CStack * attacker, const CStack * defender,
	const AttackDescriptor & attack, const BattleAttack & packet) const
{
	BattleAttackInfo primaryStrike(attacker, defender, attack.distance, attack.ranged);
	primaryStrike.physicalDamage = !packet.spellLike();
	primaryStrike.voluntaryFlightDistance = attack.voluntaryFlightDistance;
	primaryStrike.retaliation = attack.retaliation;
	primaryStrike.secondaryAttack = attack.cleaveFollowup || attack.brace || attack.bulwarkPreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.archeryCounterfire || attack.battlecraftOverwatch;
	return battlePrimaryStrikeAftermath::prepare(battle, primaryStrike, packet.bsa);
}
