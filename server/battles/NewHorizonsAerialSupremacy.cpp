/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "NewHorizonsParalyzingGaze.h"

bool BattleActionProcessor::tryAerialRetaliation(const CBattleInfoCallback & battle,
	const CStack * griffin, const CStack * shooter)
{
	const auto excursion = newHorizonsAerialSupremacy::prepare(battle, griffin, shooter);
	if(!excursion)
		return false;
	makeAttack(battle, griffin, shooter, {.targetHex = shooter->getPosition(), .first = true,
		.counter = true, .retaliation = true, .aerialExcursion = excursion});
	return true; // Cancellation is still the one admitted ordinary opportunity.
}

bool BattleActionProcessor::admitAttack(const CBattleInfoCallback & battle, const CStack * attacker,
	const CStack * defender, const AttackDescriptor & attack)
{
	if(!attacker || !attacker->alive() || newHorizonsFrozen::isFrozen(*attacker) || attacker->armorerLastStandEndedActivation
		|| (defender && !defender->alive()))
		return false;
	if(attack.aerialExcursion && !newHorizonsAerialSupremacy::matches(battle, *attack.aerialExcursion))
		return false;
	if(attack.counter && !attack.brace && !attack.bulwarkPreemptive
		&& attack.preemptiveDamagePercent <= 0 && !attack.archeryCounterfire
		&& !attack.battlecraftOverwatch && defender
		&& newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, battle, *attacker, *defender, attack.ranged,
			attack.aerialExcursion ? &*attack.aerialExcursion : nullptr))
		return false;
	return true;
}

void BattleActionProcessor::performOrdinaryRangedRetaliation(const CBattleInfoCallback & battle,
	const CStack * retaliator, const CStack * shooter)
{
	if(tryAerialRetaliation(battle, retaliator, shooter))
		return;
	if(retaliator && shooter && retaliator->alive() && shooter->alive()
		&& retaliator->hasBonusOfType(BonusType::RANGED_RETALIATION)
		&& !shooter->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
		&& retaliator->ableToRetaliate()
		&& battle.battleCanShoot(retaliator, shooter->getPosition()))
		makeAttack(battle, retaliator, shooter, {.targetHex = shooter->getPosition(), .first = true,
			.ranged = true, .counter = true, .retaliation = true});
}

bool BattleActionProcessor::canResolveAttack(const CBattleInfoCallback & battle, const CStack * attacker,
	const CStack * defender, const AttackDescriptor & attack) const
{
	return attacker->alive() && !newHorizonsFrozen::isFrozen(*attacker)
		&& (!defender || defender->alive())
		&& (!attack.aerialExcursion || newHorizonsAerialSupremacy::matches(battle, *attack.aerialExcursion));
}
