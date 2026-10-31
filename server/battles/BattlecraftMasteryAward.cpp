/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlecraftMasteryAward.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battlecraftMasteryAward
{
void apply(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const CStack * stack, BattlecraftMasteryAction action)
{
	const auto * state = battle.getBattle();
	if(!state || !stack)
		return;
	const auto side = battle.playerToSide(battle.battleGetOwner(stack));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	const auto round = state->getRound();
	const auto previousAwardRound = state->getBattlecraftMasteryAwardRound(side);
	if(!newHorizonsBattlecraft::canAwardBattlefieldMastery(battle.battleGetOwnerHero(stack), stack,
		round, previousAwardRound, action))
		return;

	SetBattlecraftMasteryAward award;
	award.battleID = state->getBattleID();
	award.side = side;
	award.unitId = stack->unitId();
	award.round = round;
	award.action = action;
	gameHandler.sendAndApply(award);
}

}
