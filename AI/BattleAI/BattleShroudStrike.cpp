/*
 * BattleShroudStrike.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "BattleShroudStrike.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsShroud.h"

namespace
{

bool evasiveShroud(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, bool flanking)
{
	if(!flanking)
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return newHorizonsShroud::hasEvasiveShroud(battle.battleGetFightingHero(side));
}

bool ambusher(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, bool flanking)
{
	if(!flanking)
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return newHorizonsShroud::ambusherDamagePercent(
		battle.battleGetFightingHero(side), attack.attacker) > 0;
}

std::optional<BattleSide> shadowAssaultSide(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack, bool flanking)
{
	if(!flanking)
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	if(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		battle.battleGetFightingHero(side), attack.defender, side) <= 0)
		return {};
	return side;
}
}

BattleShroudStrike evaluateBattleShroudStrike(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	const bool flanking = battle.battleIsShroudFlankingAttack(attack);
	return {evasiveShroud(battle, attack, flanking), ambusher(battle, attack, flanking),
		shadowAssaultSide(battle, attack, flanking)};
}

std::optional<BattleSide> qualifyingShadowAssaultSide(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return shadowAssaultSide(battle, attack, battle.battleIsShroudFlankingAttack(attack));
}

void applySelectedNightProwler(HypotheticBattle & battle, battle::CUnitState & actor, const BattleHex & originalPosition, const BattleHex & destination)
{
	const auto attackerSide = battle.playerToSide(battle.battleGetOwner(&actor));
	const auto * attackerHero = attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER
		? battle.battleGetFightingHero(attackerSide) : nullptr;
	const bool canCheckNightProwlerPath = destination.isValid()
		&& destination != originalPosition
		&& !actor.hasBonusOfType(BonusType::FLYING)
		&& attackerHero && newHorizonsShroud::rank(attackerHero) > 0
		&& newHorizonsShroud::hasNightProwler(attackerHero);
	BattleHexArray nightProwlerPath;
	if(canCheckNightProwlerPath)
		nightProwlerPath = battle.getPath(originalPosition, destination, &actor).first;
	const bool crossesNightProwlerEnemy = !nightProwlerPath.empty()
		&& battle.battleNightProwlerCrossesEnemy(&actor, nightProwlerPath);
	if(crossesNightProwlerEnemy)
		battle.addUnitBonus(actor.unitId(), newHorizonsShroud::nightProwlerDamageBonuses());
}
