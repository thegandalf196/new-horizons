/*
 * SecondWindOpportunity.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SecondWindOpportunity.h"
#include "HypotheticBattle.h"

namespace heroOrderValue
{
std::shared_ptr<const CBattleInfoCallback> projectSecondWindActivation(const CBattleInfoCallback & battle,
	const Environment * environment, const std::shared_ptr<CBattleInfoCallback> & source,
	BattleSide side, uint32_t recipientId)
{
	if(!environment || !source || source.get() != &battle || !battle.getBattle()
		|| !heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
		return {};
	// nextTurn owns activation-start status transitions, but not the live flow's
	// separate obstacle triggers. Do not infer impossibility from an incomplete
	// activation forecast.
	if(!battle.battleGetAllObstacles().empty())
		return {};
	const auto perspective = battle.battleGetMySide();
	if(perspective != BattleSide::ALL_KNOWING && perspective != side)
		return {};
	auto order = battle.battlePrepareHeroOrderState(side, HeroCommand::SECOND_WIND, {recipientId});
	if(!order)
		return {};
	auto activation = std::make_shared<HypotheticBattle>(environment, source);
	const auto allowance = activation->prepareHeroOrderAllowance(side);
	const auto effects = allowance
		? activation->beginProjectedHeroOrder(side, HeroCommand::SECOND_WIND, {recipientId}, *allowance)
		: std::optional<heroCommands::PreparedOrderEffects>();
	if(!effects || !activation->projectAcceptedHeroOrder(side, *effects, *allowance))
		return {};
	// An intervening Double Command choice may change the board. Do not invent
	// that choice or silently treat its later extra activation as immediate.
	const auto & continuation = activation->getDoubleCommandState(side);
	if(continuation.orderPending() || continuation.secondWindReady())
		return {};
	order->secondWindActive = true;
	activation->setHeroOrderState(side, *order);
	activation->nextTurn(recipientId, BattleUnitTurnReason::HERO_COMMAND);
	// Start damage can involve native probabilistic rebirth not sampled by the
	// detached lifecycle. A missing/dead forecast is not proof of no action.
	const auto * recipient = activation->battleGetUnitByID(recipientId);
	if(!recipient || !recipient->alive())
		return {};
	return activation;
}

bool hasSecondWindAttackOpportunity(const CBattleInfoCallback & activation,
	uint32_t recipientId, uint32_t enemyId)
{
	const auto * recipient = activation.battleGetUnitByID(recipientId);
	const auto * enemy = activation.battleGetUnitByID(enemyId);
	if(!recipient || !enemy || !recipient->alive() || !enemy->alive() || !recipient->canMove())
		return false;
	const auto side = activation.playerToSide(activation.battleGetOwner(recipient));
	const auto perspective = activation.battleGetMySide();
	const bool heroKnown = perspective == BattleSide::ALL_KNOWING || perspective == side;
	for(const auto & hex : enemy->getHexes())
	{
		if(activation.battleCanShootAction(recipient, hex))
			return true;
		if(activation.battleCanAttackUnitAction(recipient, enemy)
			&& activation.battleCanAttackHex(recipient, hex))
			return true;
		if(heroKnown && !activation.battleGetSkirmisherAttackFromHexes(recipient, hex).empty())
			return true;
	}
	return false;
}
}
