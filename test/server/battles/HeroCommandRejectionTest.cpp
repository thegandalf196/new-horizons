/*
 * HeroCommandRejectionTest.cpp, part of VCMI engine
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

class HeroCommandRejectionTest : public HeroCommandFixture {};

TEST_F(HeroCommandRejectionTest, RejectedCommandRestoresControlWithoutStartingActionOrExpiringBonuses)
{
	prepareCommands();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto activeUnitId = active->unitId();
	const auto orderUsed = battle()->getHeroCommandUsed(BattleSide::ATTACKER);
	Bonus temporary;
	temporary.type = BonusType::STACKS_SPEED;
	temporary.val = 5;
	temporary.duration = BonusDuration::STACK_GETS_TURN;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(active->unitId(), std::vector<Bonus>{temporary});
	gameHandler->sendAndApply(effect);
	const auto speed = active->getMovementRange();
	const auto activations = server.stackActivations.size();
	const auto starts = server.startedActions.size();

	ASSERT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(server.startedActions.size(), starts);
	ASSERT_EQ(server.stackActivations.size(), activations + 1);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(server.stackActivations.back().stack, activeUnitId);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeUnitId);
	EXPECT_EQ(battle()->getHeroCommandUsed(BattleSide::ATTACKER), orderUsed);
	EXPECT_EQ(active->getMovementRange(), speed);
	EXPECT_FALSE(active->getAllBonuses(Bonus::UntilGetsTurn)->empty());
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
}
