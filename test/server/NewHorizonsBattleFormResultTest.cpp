/*
 * NewHorizonsBattleFormResultTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "battles/BattleTestFixture.h"

#include "../../lib/battle/SideInBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/battles/BattleResultProcessor.h"
#include "../../server/queries/BattleQueries.h"
#include "../../server/queries/QueriesProcessor.h"

namespace
{
CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

class NewHorizonsBattleFormResultTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		// Keep the attacker computer controlled so result capture waits for the
		// normal battle-result dialog instead of immediately removing the battle.
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	CStack * stackAt(BattleSide side, SlotID slot) const
	{
		const auto stacks = battle()->battleGetStacksIf([side, slot](const CStack * stack)
		{
			return stack->unitSide() == side && stack->unitSlot() == slot;
		});
		if(stacks.size() != 1)
			return nullptr;
		return const_cast<CStack *>(stacks.front());
	}

	std::shared_ptr<CBattleQuery> installBattleQuery()
	{
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		return query;
	}

	void acceptBattleResult()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
		}
	}
};
}

TEST_F(NewHorizonsBattleFormResultTest, CasualtyCountUsesSourceSpeciesAndLeavesLiveFormUnchanged)
{
	const auto pikeman = creature("core:pikeman");
	const auto skeleton = creature("core:skeleton");
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 10));
	startBattle();

	auto * stack = stackAt(BattleSide::ATTACKER, SlotID(0));
	ASSERT_NE(stack, nullptr);
	stack->beginBattleForm(skeleton, 2);
	ASSERT_TRUE(stack->hasBattleForm());
	ASSERT_NE(stack->getCount(), 10);

	int64_t damage = 10;
	stack->damage(damage);
	ASSERT_EQ(damage, 10);
	int64_t oneBattleResurrection = 10;
	stack->heal(oneBattleResurrection, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(oneBattleResurrection, 10);
	ASSERT_EQ(stack->getKilled(), 1);

	const JsonNode liveStateBeforeProjection = stack->save();
	CasualtiesAfterBattle projected(*battle(), BattleSide::ATTACKER);
	ASSERT_EQ(projected.newStackCounts.size(), 1u);
	EXPECT_EQ(projected.newStackCounts.front().second, 9);
	EXPECT_EQ(stack->save(), liveStateBeforeProjection);
	EXPECT_TRUE(stack->hasBattleForm());
	EXPECT_EQ(stack->creatureId(), skeleton);
}

TEST_F(NewHorizonsBattleFormResultTest, EarlyResultUsesOriginalCasualtyAndNecromancyIdentityAndGatedCount)
{
	const auto pikeman = creature("core:pikeman");
	const auto skeleton = creature("core:skeleton");
	const auto imp = creature("core:imp");
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 10));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), skeleton, 10));
	startBattle();

	auto * pikemanStack = stackAt(BattleSide::DEFENDER, SlotID(0));
	auto * skeletonStack = stackAt(BattleSide::DEFENDER, SlotID(1));
	ASSERT_NE(pikemanStack, nullptr);
	ASSERT_NE(skeletonStack, nullptr);
	pikemanStack->beginBattleForm(skeleton, 2);
	skeletonStack->beginBattleForm(pikeman, 2);

	CStack * gatedStack = addStack(BattleSide::ATTACKER, imp, BattleHex(leftHex), 10);
	ASSERT_NE(gatedStack, nullptr);
	gatedStack->beginBattleForm(skeleton, 2);
	ASSERT_NE(gatedStack->getCount(), 10);
	const JsonNode gatedStateBeforeResult = gatedStack->save();
	SideInBattle::GatedDemonicStack gated;
	gated.unitId = gatedStack->unitId();
	gated.creature = imp;
	gated.initialCount = 10;
	battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks.push_back(gated);

	// The compact battle fixture applies BattleStart directly and therefore
	// omits the query installed by the production BattleProcessor path. Install
	// the standard query so early victory follows normal result capture and
	// gated-reserve reconciliation.
	const auto battleQuery = installBattleQuery();
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	ASSERT_TRUE(battleQuery->result);
	const auto * result = &*battleQuery->result;
	EXPECT_EQ(result->casualties[BattleSide::DEFENDER].at(pikeman), 10);
	EXPECT_EQ(result->casualties[BattleSide::DEFENDER].at(skeleton), 10);
	EXPECT_EQ(result->necromancyEligibleCasualties[BattleSide::DEFENDER].at(pikeman), 10);
	EXPECT_EQ(result->necromancyEligibleCasualties[BattleSide::DEFENDER].count(skeleton), 0u);
	EXPECT_EQ(gatedStack->save(), gatedStateBeforeResult);
	EXPECT_TRUE(gatedStack->hasBattleForm());
	EXPECT_EQ(gatedStack->creatureId(), skeleton);

	acceptBattleResult();
	EXPECT_EQ(attackerSideHero->getDemonicReserveCount(imp), 10);
}
