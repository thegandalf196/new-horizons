/*
 * WarMachineControlTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"

class WarMachineControlTest : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
};

// Characterizes server routing, not SDL client dispatch or protection against duplicate client commands.
TEST_P(WarMachineControlTest, BallistaArtilleryControlsAuthoritativeTurnRouting)
{
	const int artilleryRank = GetParam();
	startGame(); // Keeps the fixture's seed 1337 and the production randomizer unchanged.
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARTILLERY, artilleryRank, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();

	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();

	// Keep a live enemy army even if the automatic shot kills the fixture's one-creature token.
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(target->alive());
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));

	const auto ballistaActivated = [&]()
	{
		return std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & activation)
		{
			return activation.battleID == BattleID(0) && activation.stack == ballista->unitId();
		});
	};

	beginCombat();
	const auto firstRound = battle()->getRound();
	// Follow only real client-eligible turns; never assign activeStack or invoke private flow helpers.
	// Two opportunities per stack bound the traversal even if a unit gains a morale turn.
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns && !ballistaActivated(); ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_TRUE(ballistaActivated());

	std::vector<BattleSetActiveStack> activations;
	std::vector<StartAction> actions;
	for(const auto & activation : server.stackActivations)
		if(activation.battleID == BattleID(0) && activation.stack == ballista->unitId())
			activations.push_back(activation);
	for(const auto & action : server.startedActions)
		if(action.battleID == BattleID(0) && action.ba.isUnitAction() && action.ba.stackNumber == ballista->unitId())
			actions.push_back(action);

	ASSERT_EQ(activations.size(), 1u);
	if(artilleryRank == 0)
	{
		EXPECT_EQ(activations.front().reason, BattleUnitTurnReason::AUTOMATIC_ACTION);
		ASSERT_EQ(actions.size(), 1u);
		EXPECT_EQ(actions.front().ba.actionType, EActionType::SHOOT);
		EXPECT_EQ(actions.front().ba.side, BattleSide::ATTACKER);
	}
	else
	{
		EXPECT_EQ(activations.front().reason, BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_TRUE(actions.empty());
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		EXPECT_EQ(battle()->battleActiveUnit()->unitId(), ballista->unitId());
	}
}

INSTANTIATE_TEST_SUITE_P(ArtilleryRanks, WarMachineControlTest, ::testing::Values(0, 1, 2, 3));

class FirstAidTentControlTest : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
protected:
	void checkRouting(bool woundTarget)
	{
		const int firstAidRank = GetParam();
		startGame(); // Retains seed 1337 and all production randomizer calls.
		attackerSideHero->setSecSkillLevel(SecondarySkill::FIRST_AID, firstAidRank, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		startBattle();

		const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::ATTACKER && stack->isFirstAidTent();
		});
		ASSERT_EQ(machines.size(), 1u);
		const auto * tent = machines.front();
		const auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		ASSERT_NE(target, nullptr);
		ASSERT_TRUE(target->alive());
		ASSERT_EQ(target->unitOwner(), tent->unitOwner());
		ASSERT_FALSE(target->canBeHealed());
		ASSERT_GT(target->getMaxHealth(), 1);

		if(woundTarget)
		{
			// A partial wound, not a casualty: apply the ordinary injury packet without
			// modifying unit state directly or substituting the production randomizer.
			StacksInjured injury;
			injury.battleID = BattleID(0);
			auto & attacked = injury.stacks.emplace_back();
			attacked.stackAttacked = target->unitId();
			attacked.damageAmount = 1;
			target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
			ASSERT_EQ(attacked.killedAmount, 0u);
			gameHandler->sendAndApply(injury);
			ASSERT_TRUE(target->alive());
			ASSERT_TRUE(target->canBeHealed());
		}

		const auto healable = battle()->battleGetStacksIf([&](const CStack * stack)
		{
			return stack->unitOwner() == tent->unitOwner() && stack->canBeHealed();
		});
		if(woundTarget)
		{
			ASSERT_EQ(healable.size(), 1u);
			ASSERT_EQ(healable.front(), target);
		}
		else
			ASSERT_TRUE(healable.empty());

		const auto tentActivated = [&]()
		{
			return std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [&](const auto & activation)
			{
				return activation.battleID == BattleID(0) && activation.stack == tent->unitId();
			});
		};

		ASSERT_NO_FATAL_FAILURE(beginCombat());
		const auto firstRound = battle()->getRound();
		const auto maximumTurns = battle()->stacks.size() * 2;
		for(size_t turn = 0; turn < maximumTurns && !tentActivated(); ++turn)
		{
			ASSERT_EQ(battle()->getRound(), firstRound);
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_NE(active->unitId(), tent->unitId());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		ASSERT_TRUE(tentActivated());

		std::vector<BattleSetActiveStack> activations;
		std::vector<StartAction> actions;
		for(const auto & activation : server.stackActivations)
			if(activation.battleID == BattleID(0) && activation.stack == tent->unitId())
				activations.push_back(activation);
		for(const auto & action : server.startedActions)
			if(action.battleID == BattleID(0) && action.ba.isUnitAction() && action.ba.stackNumber == tent->unitId())
				actions.push_back(action);

		ASSERT_EQ(activations.size(), 1u);
		if(!woundTarget || firstAidRank == 0)
		{
			EXPECT_EQ(activations.front().reason, BattleUnitTurnReason::AUTOMATIC_ACTION);
			ASSERT_EQ(actions.size(), 1u);
			EXPECT_EQ(actions.front().ba.side, BattleSide::ATTACKER);
			EXPECT_EQ(actions.front().ba.actionType, woundTarget ? EActionType::STACK_HEAL : EActionType::NO_ACTION);
			if(woundTarget)
			{
				const auto destinations = actions.front().ba.getTarget(battle());
				ASSERT_EQ(destinations.size(), 1u);
				EXPECT_EQ(destinations.front().unitValue, target);
				EXPECT_FALSE(target->canBeHealed());
			}
		}
		else
		{
			EXPECT_EQ(activations.front().reason, BattleUnitTurnReason::TURN_QUEUE);
			EXPECT_TRUE(actions.empty());
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			EXPECT_EQ(battle()->battleActiveUnit()->unitId(), tent->unitId());
			EXPECT_TRUE(target->canBeHealed());
		}
	}
};

TEST_P(FirstAidTentControlTest, WoundedAllyUsesSkillGatedAuthoritativeTurnRouting)
{
	checkRouting(true);
}

INSTANTIATE_TEST_SUITE_P(FirstAidRanks, FirstAidTentControlTest, ::testing::Values(0, 1, 2, 3));

class FirstAidTentNoTargetControlTest : public FirstAidTentControlTest
{
};

TEST_P(FirstAidTentNoTargetControlTest, NoWoundedAllyForcesAutomaticNoActionEvenWithSkill)
{
	checkRouting(false);
}

INSTANTIATE_TEST_SUITE_P(FirstAidRanks, FirstAidTentNoTargetControlTest, ::testing::Values(0, 1));
