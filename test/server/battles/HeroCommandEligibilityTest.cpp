/*
 * HeroCommandEligibilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

class HeroCommandEligibilityTest : public HeroCommandFixture {};

TEST_F(HeroCommandEligibilityTest, SuccessfulOrderDoesNotExpirePreexistingUnitTurnBonus)
{
	prepareCommands();
	const auto * active = battle()->battleActiveUnit();
	Bonus temporary;
	temporary.type = BonusType::STACKS_SPEED;
	temporary.val = 5;
	temporary.duration = BonusDuration::STACK_GETS_TURN;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(active->unitId(), std::vector<Bonus>{temporary});
	gameHandler->sendAndApply(effect);
	const auto speed = active->getMovementRange();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());
	EXPECT_EQ(active->getMovementRange(), speed);
	EXPECT_FALSE(active->getAllBonuses(Bonus::UntilGetsTurn)->empty());
}

TEST_F(HeroCommandEligibilityTest, TacticsRejectionPreservesBudgetAndOnlyRestoresControl)
{
	prepareCommands();
	// Fixture-only setup isolates tactics from the otherwise eligible active hero.
	battle()->tacticDistance = 1;
	battle()->tacticsSide = BattleSide::ATTACKER;
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto activeUnitId = active->unitId();
	const auto starts = server.startedActions.size();
	const auto activations = server.stackActivations.size();
	size_t rejectedCommands = 0;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		HeroCommand::ADVANCE, HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
	{
		++rejectedCommands;
		EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, command));
		EXPECT_FALSE(issue(command));
	}
	EXPECT_EQ(server.startedActions.size(), starts);
	// Each rejection returns control to the same stack without a new turn activation.
	ASSERT_EQ(server.stackActivations.size(), activations + rejectedCommands);
	for(auto i = activations; i < server.stackActivations.size(); ++i)
	{
		EXPECT_EQ(server.stackActivations[i].reason, BattleUnitTurnReason::ACTION_REJECTED);
		EXPECT_EQ(server.stackActivations[i].stack, activeUnitId);
	}
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeUnitId);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	battle()->tacticDistance = 0;
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
}

TEST_F(HeroCommandEligibilityTest, MissingCommanderRejectionPreservesBudget)
{
	prepareCommands();
	// Isolate the missing-commander guard in the authoritative fixture's side
	// snapshot. This is not a claim of a completed town-garrison GUI journey.
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto hero = side.heroID;
	side.heroID = ObjectInstanceID();
	ASSERT_EQ(battle()->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto activeUnitId = active->unitId();
	const auto starts = server.startedActions.size();
	const auto activations = server.stackActivations.size();
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(server.startedActions.size(), starts);
	ASSERT_EQ(server.stackActivations.size(), activations + 1);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(server.stackActivations.back().stack, activeUnitId);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeUnitId);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	side.heroID = hero;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
}

TEST_F(HeroCommandEligibilityTest, InvalidSideIsRejectedWithoutFlowOrBudgetChange)
{
	prepareCommands();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto activeUnitId = active->unitId();
	const auto starts = server.startedActions.size();
	const auto activations = server.stackActivations.size();
	auto invalid = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::AGGRESSIVE);
	invalid.side = static_cast<BattleSide>(127);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), invalid));
	EXPECT_EQ(server.startedActions.size(), starts);
	ASSERT_EQ(server.stackActivations.size(), activations + 1);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(server.stackActivations.back().stack, activeUnitId);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeUnitId);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
}
