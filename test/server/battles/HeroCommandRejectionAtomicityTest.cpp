/*
 * HeroCommandRejectionAtomicityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
enum class RejectionCase
{
	WRONG_SENDER,
	WRONG_SIDE,
	INVALID_COMMAND,
	UNEXPECTED_TARGET,
	MISSING_BATTLE
};
}

class HeroCommandRejectionAtomicityTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<RejectionCase>
{
};

TEST_P(HeroCommandRejectionAtomicityTest, FreshBudgetRejectionPreservesUnitAndAllowsValidCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto activeId = active->unitId();
	const auto originalSpeed = active->getMovementRange();
	Bonus temporary;
	temporary.type = BonusType::STACKS_SPEED;
	temporary.val = 5;
	temporary.duration = BonusDuration::STACK_GETS_TURN;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(activeId, std::vector<Bonus>{temporary});
	gameHandler->sendAndApply(effect);
	const auto speed = active->getMovementRange();
	ASSERT_GT(speed, originalSpeed); // Ensure the expiry sentinel is observable.
	const auto starts = server.startedActions.size();
	const auto activations = server.stackActivations.size();
	// Nonzero test sentinels, without giving either hero a spellbook.
	attackerSideHero->mana = 37;
	defenderSideHero->mana = 23;
	const auto mana = attackerSideHero->mana;
	const auto defenderMana = defenderSideHero->mana;
	ASSERT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	ASSERT_FALSE(battle()->getHeroCommandUsed(BattleSide::DEFENDER));

	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	auto battleId = BattleID(0);
	auto sender = PlayerColor(0);
	switch(GetParam())
	{
		case RejectionCase::WRONG_SENDER:
			sender = PlayerColor(1);
			break;
		case RejectionCase::WRONG_SIDE:
			action.side = BattleSide::DEFENDER;
			break;
		case RejectionCase::INVALID_COMMAND:
			action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, static_cast<HeroCommand>(127));
			break;
		case RejectionCase::UNEXPECTED_TARGET:
			action.aimToUnit(active);
			break;
		case RejectionCase::MISSING_BATTLE:
			battleId = BattleID(1);
			ASSERT_EQ(gameHandler->gameState().getBattle(battleId), nullptr);
			break;
	}
	ASSERT_FALSE(gameHandler->battles->makePlayerBattleAction(battleId, sender, action));
	EXPECT_EQ(server.startedActions.size(), starts);
	EXPECT_EQ(server.stackActivations.size(), activations);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeId);
	EXPECT_EQ(battle()->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(defenderSideHero->mana, defenderMana);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::DEFENDER));
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		EXPECT_EQ(battle()->battleGetActiveOrder(side), HeroCommand::NONE);
		EXPECT_EQ(battle()->battleGetActiveDoctrine(side), HeroCommand::NONE);
	}
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeId);
	EXPECT_EQ(battle()->battleActiveUnit()->getMovementRange(), speed);
	ASSERT_EQ(server.stackActivations.size(), activations + 1);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(defenderSideHero->mana, defenderMana);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::DEFENDER));
}

INSTANTIATE_TEST_SUITE_P(InvalidFirstRequests, HeroCommandRejectionAtomicityTest,
	::testing::Values(RejectionCase::WRONG_SENDER, RejectionCase::WRONG_SIDE,
		RejectionCase::INVALID_COMMAND, RejectionCase::UNEXPECTED_TARGET, RejectionCase::MISSING_BATTLE));
