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
