/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "BattleProxy.h"
#include "../gameState/CGameState.h"
#include "../gameState/GameStatePackVisitor.h"

const MetamagicPointState & BattleInfo::getMetamagicPointState(BattleSide side) const
{
	return sides.at(side).metamagicPointState;
}

void BattleInfo::setMetamagicPointState(BattleSide side, const MetamagicPointState & state)
{
	state.validate();
	sides.at(side).metamagicPointState = state;
}

void BattleInfo::validateMetamagicPointSerialization(bool supported) const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		getMetamagicPointState(side).validateSerialization(supported);
}

const SpellResponseState & BattleInfo::getSpellResponseState(BattleSide side) const
{
	static const SpellResponseState empty;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return empty;
	return sides.at(side).spellResponseState;
}

const MetamagicPointState & BattleProxy::getMetamagicPointState(BattleSide side) const
{
	return subject->getBattle()->getMetamagicPointState(side);
}

void SetMetamagicPointState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetMetamagicPointState(*this);
}

void GameStatePackVisitor::visitSetMetamagicPointState(SetMetamagicPointState & pack)
{
	pack.validateShape();
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Metamagic point update");
	BattleStatePackVisitor visitor(*battle);
	pack.visitTyped(visitor);
}

void BattleStatePackVisitor::visitSetMetamagicPointState(SetMetamagicPointState & pack)
{
	pack.validateShape();
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Metamagic point update targets another battle");
	pack.validateTransitionFrom(battleState.getMetamagicPointState(pack.side), battleState.getRound());
	battleState.setMetamagicPointState(pack.side, pack.state);
}

int32_t BattleInfo::getMetamagicPendingCount(BattleSide side) const
{
	return getSide(side).metamagicPendingCount;
}
