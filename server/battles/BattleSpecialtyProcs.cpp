/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleSpecialtyProcs.h"
#include "BattleProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/CStack.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace battleSpecialtyProcs
{
void resolveStun(CGameHandler & game, BattleProcessor & processor, const CBattleInfoCallback & battle,
	uint32_t source, uint32_t target, int nominalChance)
{
	if(nominalChance < 0 || nominalChance > 100) throw std::invalid_argument("Invalid Concussive Stun chance");
	const auto * actor = battle.battleGetUnitByID(source);
	const auto * recipient = battle.battleGetUnitByID(target);
	if(nominalChance <= 0 || !actor || !recipient || !actor->alive() || !recipient->alive()
		|| actor->isTimeStopped() || actor->isGhost() || recipient->isTimeStopped() || recipient->isGhost()
		|| recipient->isInvincible() || recipient->isTurret() || battle.battleGetOwner(actor) == battle.battleGetOwner(recipient)
		|| newHorizonsStun::isStunned(*recipient)) return;
	const auto side = battle.playerToSide(battle.battleGetOwner(recipient));
	const auto army = battle.getBattle()->getSideArmy(actor->unitSide())->id;
	const int chance = game.randomizer->prepareFavorableCreatureAbilityChance(*actor, nominalChance);
	const auto draw = [&game, army, chance]() { return game.randomizer->rollCombatAbility(army, chance); };
	if(!processor.resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), side,
		chance > 0 && chance < 100, true, draw)) return;
	SetStackEffect applied;
	applied.battleID = battle.getBattle()->getBattleID();
	applied.toAdd.emplace_back(target, std::vector<Bonus>{newHorizonsStun::marker()});
	game.sendAndApply(applied);
}
}
