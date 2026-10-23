/*
 * PrimaryFortuneReceipt.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PrimaryFortuneReceipt.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"

std::optional<PrimaryFortuneReceipt> captureAppliedFortuneReceipt(
	const HypotheticBattle & battle, const BattleAttackInfo & attack, bool evaluateOnly)
{
	if(evaluateOnly)
		return std::nullopt;

	PrimaryFortuneReceipt result;
	result.perfectFortune = battle.battleCanTriggerPerfectFortune(attack);
	result.controllerSide = battle.playerToSide(battle.battleGetActionController(attack.attacker));
	result.ordinaryLuckAttack = !attack.secondaryAttack
		&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage);
	result.resolvedLuck = battle.captureFortuneStrikeOutcome(attack);
	return result;
}
