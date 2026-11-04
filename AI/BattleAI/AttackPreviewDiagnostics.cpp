/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackPreviewDiagnostics.h"
#include "AttackPossibility.h"
#include "../../lib/CCreatureHandler.h"
namespace attackPreviewDiagnostics
{
void result(const char * label, const AttackPossibility & ap, const BattleAttackInfo & attack)
{
	logAi->trace("%s: %s -> %s at %d from %d, affects %d units: d:%lld a:%lld c:%lld s:%lld",
		label, attack.attacker->unitType()->getJsonKey(), attack.defender->unitType()->getJsonKey(),
		ap.dest.toInt(), ap.from.toInt(), (int)ap.affectedUnits.size(), ap.defenderDamageReduce,
		ap.attackerDamageReduce, ap.collateralDamageReduce, ap.shootersBlockedDmg);
}
}
