/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleSylvanStrikeAftermath.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/combatScripts/CombatEventPayload.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleSylvanStrikeAftermath
{
void finish(CGameHandler & game, const CBattleInfoCallback & battle, const CStack * attacker,
	const battle::Unit * defender, BattleAttack & bat, const CombatEventPayload & payload,
	battle::CUnitState & attackerState, bool ranged)
{
	if(!bat.fortuneState) return;
	newHorizonsSylvanTactics::finishStrike(battle, attacker, defender, bat.bsa,
	 ranged, newHorizonsCombatSkills::isPhysicalAttackDamage(attacker, ranged, bat.spellLike()),
		bat.fortuneState->tactics);
	if(!bat.lucky() || bat.fortuneState->tactics.enabled) return;
	int64_t actualDamage = 0;
	bool enemyStackKilled = false;
	auto adjacentFriends = battle.battleFortuneAdjacentFriends(attacker);
	for(const auto & target : payload.targets)
	{
		const auto victim = std::find_if(bat.bsa.begin(), bat.bsa.end(), [&target](const auto & hit) { return hit.newState.id == target.unit->unitId(); });
		const bool destroyed = victim != bat.bsa.end() && victim->killed() && !victim->willRebirth();
		if(destroyed)
			vstd::erase(adjacentFriends, target.unit->unitId());
		// In classic single-target Luck mode, collateral victims did not
		// receive a Lucky Strike and cannot fuel Recovery or Cascading.
		if(target.unit != defender && !game.gameInfo().getSettings().getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS))
			continue;
		actualDamage += std::min(target.damage, target.healthBeforeAttack);
		if(destroyed && battle.battleMatchOwner(attacker, target.unit))
			enemyStackKilled = true;
	}
	if(bat.fortuneState)
		bat.fortuneState->finishPositiveStrike(adjacentFriends, enemyStackKilled);
	if(!ranged && attackerState.alive())
	{
		auto healing = newHorizonsCombatSkills::luckyRecoveryAmount(actualDamage,
			false, bat.fortuneState && bat.fortuneState->luckyRecovery);
		if(healing > 0)
			attackerState.heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
}
}
