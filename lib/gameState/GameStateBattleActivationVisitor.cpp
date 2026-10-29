/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../CStack.h"
#include "../battle/BattleInfo.h"
#include "../networkPacks/PacksForClientBattle.h"
void GameStatePackVisitor::visitBattleSetActiveStack(BattleSetActiveStack & pack)
{
	gs.getBattle(pack.battleID)->nextTurn(pack.stack, pack.reason);
}

void GameStatePackVisitor::visitEndAction(EndAction & pack)
{
	if(pack.endsFortuneActivation)
	{
		auto * battle = gs.getBattle(pack.battleID);
		if(!battle)
			throw std::runtime_error("EndAction references a missing battle");
		if(auto * activeStack = battle->getStack(battle->activeStack, false))
		{
			activeStack->setActivationMovementBonus(0);
			activeStack->forestAmbush.finishActivation();
		}
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			battle->getSide(side).sylvanLuck.endActivation();
	}
}

void GameStatePackVisitor::visitBattleNormalActivationCompleted(BattleNormalActivationCompleted & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle || !pack.expected.enabled() || pack.expected.active != pack.unitId
		|| battle->getSeizeInitiativeState() != pack.expected)
		throw std::runtime_error("Stale normal activation completion receipt");
	auto next = pack.expected;
	next.complete(pack.unitId);
	battle->setSeizeInitiativeState(next);
}
