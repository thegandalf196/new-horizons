/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsElementalAttunement.h"
#include "../gameState/CGameState.h"
#include "../gameState/GameStatePackVisitor.h"
#include "../networkPacks/SetElementalAttunementState.h"

void SetElementalAttunementState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetElementalAttunementState(*this);
}
void GameStatePackVisitor::visitSetElementalAttunementState(SetElementalAttunementState & pack)
{
	pack.validateShape();
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle) throw std::runtime_error("Missing battle for elemental Attunement update");
	BattleStatePackVisitor visitor(*battle);
	pack.visitTyped(visitor);
}
void BattleStatePackVisitor::visitSetElementalAttunementState(SetElementalAttunementState & pack)
{
	pack.validateShape();
	const auto * battle = dynamic_cast<const CBattleInfoCallback *>(&battleState);
	const auto prior = battleState.getElementalAttunementMask(pack.side);
	if(!battle || pack.battleID != battleState.getBattleID() || prior != pack.beforeMask
		|| newHorizonsElementalAttunement::completedHeroCastMask(*battle, pack.side, pack.spellID, prior) != pack.afterMask)
		throw std::runtime_error("Stale or invalid completed elemental Attunement update");
	battleState.setElementalAttunementMask(pack.side, pack.afterMask);
}
