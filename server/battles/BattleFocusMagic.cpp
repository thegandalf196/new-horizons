/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleFocusMagic.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>

namespace battleFocusMagic
{
void capturePresentation(BattleAttack & attack, const battle::Unit & attacker)
{
	attack.focusMagicRanged = attack.shot() && newHorizonsFocusMagic::active(attacker);
	attack.focusMagicConverted = attack.focusMagicRanged && newHorizonsFocusMagic::converted(attacker);
}

void consumeAcceptedStrike(ServerCallback & server, const CBattleInfoCallback & battle,
	const BattleAttack & attack)
{
	if(!attack.focusMagicRanged)
		return;
	const auto * attacker = battle.battleGetUnitByID(attack.stackAttacking);
	if(!attacker || !attacker->alive() || attacker->isGhost())
		return;
	const auto plan = newHorizonsFocusMagic::consume(*attacker);
	if(plan.remove.empty())
		return;
	SetStackEffect payment;
	payment.battleID = attack.battleID;
	payment.toRemove.emplace_back(attacker->unitId(), plan.remove);
	if(!plan.add.empty())
		payment.toAdd.emplace_back(attacker->unitId(), plan.add);
	server.apply(payment);
}
}
