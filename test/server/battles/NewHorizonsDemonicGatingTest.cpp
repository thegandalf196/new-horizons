/*
 * NewHorizonsDemonicGatingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/StackLocation.h"

namespace
{
class NewHorizonsDemonicGatingTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		startGame();
		const auto imp = creatureByName("core:imp");
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), imp.toCreature()));
		attackerSideHero->setSecSkillLevel(
			SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setDemonicReserve({{imp, 12}});
		startBattle();
		beginCombat();
	}

	BattleHex legalGateHex(const battle::Unit * source) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			if(candidate.isAvailable() && BattleHex::getDistance(source->getPosition(), candidate) <= 3
				&& accessibility.accessible(candidate, false, source->unitSide()))
				return candidate;
		}
		return BattleHex();
	}

	void grantGatingPerk(const std::string & perkId)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		state.selected.clear();
		state.select("new-horizons:demonicGating", perkId, MasteryLevel::BASIC);
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:demonicGating", perkId));
	}

	BattleHex gateHexAtDistance(const battle::Unit * source, int minimum, int maximum) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			const auto distance = BattleHex::getDistance(source->getPosition(), candidate);
			if(candidate.isAvailable() && distance >= minimum && distance <= maximum
				&& accessibility.accessible(candidate, false, source->unitSide()))
				return candidate;
		}
		return BattleHex();
	}

	std::pair<BattleHex, BattleHex> adjacentGateAndEnemyHexes(const battle::Unit * source) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int gateIndex = 0; gateIndex < GameConstants::BFIELD_SIZE; ++gateIndex)
		{
			BattleHex gate(gateIndex);
			if(!gate.isAvailable() || BattleHex::getDistance(source->getPosition(), gate) > 3
				|| !accessibility.accessible(gate, false, source->unitSide()))
				continue;
			for(int enemyIndex = 0; enemyIndex < GameConstants::BFIELD_SIZE; ++enemyIndex)
			{
				BattleHex enemy(enemyIndex);
				if(enemy.isAvailable() && BattleHex::getDistance(gate, enemy) == 1
					&& accessibility.accessible(enemy, false, BattleSide::DEFENDER))
					return {gate, enemy};
			}
		}
		return {BattleHex(), BattleHex()};
	}
};
}

TEST_F(NewHorizonsDemonicGatingTest, CommitsOwnedReserveAndArrivesAtNextRound)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).demonicReserve.empty());
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.size(), 1u);

	endRound();
	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->creatureId() == creatureByName("core:imp") && stack->getCount() == 12;
	});
	ASSERT_EQ(gated.size(), 1u);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.empty());
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks.size(), 1u);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks.front().unitId, gated.front()->unitId());
}

TEST_F(NewHorizonsDemonicGatingTest, RejectsUnavailableAndOutOfRangeSelectionsWithoutSpendingTurn)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:devil");
	action.aimToHex(BattleHex(rightHex));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(active->moved());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).demonicReserve.at(creatureByName("core:imp")), 12);
}

TEST_F(NewHorizonsDemonicGatingTest, WideGateAuthoritativelyExtendsPlacementRangeToFive)
{
	grantGatingPerk("new-horizons:demonicGating.wideGate");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = gateHexAtDistance(active, 4, 5);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
}

TEST_F(NewHorizonsDemonicGatingTest, GateBeyondThreeHexesIsRejectedWithoutWideGate)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = gateHexAtDistance(active, 4, 5);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(active->moved());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).demonicReserve.at(creatureByName("core:imp")), 12);
}

TEST_F(NewHorizonsDemonicGatingTest, SwiftGateArrivesBeforeTheRoundCounterAdvances)
{
	grantGatingPerk("new-horizons:demonicGating.swiftGate");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());
	const int32_t openingRound = battle()->getRound();
	server.unitAdditionRounds.clear();

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	ASSERT_EQ(server.unitAdditionRounds.size(), 1u);
	EXPECT_EQ(server.unitAdditionRounds.front(), openingRound);
	EXPECT_EQ(battle()->getRound(), openingRound + 1);
}

TEST_F(NewHorizonsDemonicGatingTest, HellfireArrivalDealsFifteenPercentAggregateHealthToAdjacentEnemies)
{
	grantGatingPerk("new-horizons:demonicGating.hellfireArrival");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto [destination, enemyHex] = adjacentGateAndEnemyHexes(active);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), enemyHex, 100);
	ASSERT_NE(enemy, nullptr);
	const uint32_t enemyId = enemy->unitId();
	const int64_t healthBefore = enemy->getAvailableHealth();
	server.injuries.clear();
	active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_EQ(gated.size(), 1u);
	const auto * damagedEnemy = battle()->battleGetStackByID(enemyId, false);
	ASSERT_NE(damagedEnemy, nullptr);
	const int64_t expected = gated.front()->getAvailableHealth() * 15 / 100;
	EXPECT_EQ(healthBefore - damagedEnemy->getAvailableHealth(), expected);
	ASSERT_EQ(server.injuries.size(), 1u);
	ASSERT_EQ(server.injuries.front().stacks.size(), 1u);
	EXPECT_EQ(server.injuries.front().stacks.front().damageAmount, expected);
}
