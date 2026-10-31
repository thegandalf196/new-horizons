/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <limits>
#include <stdexcept>
namespace
{
class GraveDustActivationTest : public HeroCommandFixture
{
protected:
	uint32_t prepareDust()
	{
		useRevisedOrders = true;
		prepareCommands(true);
		if(HasFatalFailure()) return std::numeric_limits<uint32_t>::max();
		const auto * active = battle()->battleActiveUnit();
		if(!active) throw std::logic_error("Fixture requires its real active unit");
		SetStackEffect dust;
		dust.battleID = battle()->getBattleID();
		dust.toAdd.emplace_back(active->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
		gameHandler->sendAndApply(dust);
		return active->unitId();
	}
	int reduction(uint32_t id)
	{
		const auto * unit = battle()->battleGetUnitByID(id);
		if(!unit) throw std::logic_error("Fixture unit disappeared");
		return battle::healingReceivedReduction(*unit);
	}
	bool act(const BattleAction & action, PlayerColor player)
	{
		return gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), player, action);
	}
};
TEST_F(GraveDustActivationTest, AcceptedWaitClearsPairAndDelayedActionIsClear)
{
	const auto id = prepareDust();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(reduction(id), 5000);
	const auto round = battle()->getRound();
	ASSERT_TRUE(act(BattleAction::makeWait(battle()->battleActiveUnit()), PlayerColor(0)));
	EXPECT_EQ(reduction(id), 0);
	for(int step = 0; step < 16 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitId() != id; ++step)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(act(BattleAction::makeDefend(active), battle()->battleGetActionController(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), id);
	ASSERT_EQ(battle()->getRound(), round);
	EXPECT_EQ(reduction(id), 0);
	EXPECT_TRUE(battle()->getStack(id)->waitedThisTurn);
}
TEST_F(GraveDustActivationTest, AcceptedDefendClearsBothRecords)
{
	const auto id = prepareDust();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(reduction(id), 5000);
	ASSERT_TRUE(act(BattleAction::makeDefend(battle()->battleActiveUnit()), PlayerColor(0)));
	EXPECT_EQ(reduction(id), 0);
}
TEST_F(GraveDustActivationTest, InactiveStackRefusalLeavesDustAndActivation)
{
	const auto id = prepareDust();
	ASSERT_FALSE(HasFatalFailure());
	const auto round = battle()->getRound();
	const auto units = battle()->battleGetUnitsIf([id](const battle::Unit * unit)
	{
		return unit && unit->alive() && unit->unitId() != id;
	});
	ASSERT_FALSE(units.empty());
	const auto invalid = BattleAction::makeWait(units.front());
	ASSERT_NE(invalid.stackNumber, static_cast<int32_t>(id));
	EXPECT_FALSE(act(invalid, battle()->battleGetActionController(units.front())));
	EXPECT_EQ(battle()->getActiveStackID(), static_cast<int32_t>(id));
	EXPECT_EQ(battle()->getRound(), round);
	EXPECT_EQ(reduction(id), 5000);
}
class GraveDustHeroContinuationTest : public GraveDustActivationTest, public ::testing::WithParamInterface<int> {};
TEST_P(GraveDustHeroContinuationTest, HeroActionDoesNotEndAnchoredUnitDust)
{
	const auto id = prepareDust();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(reduction(id), 5000);
	ASSERT_TRUE(act(heroAction(GetParam()), PlayerColor(0)));
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), id);
	EXPECT_EQ(reduction(id), 5000);
	ASSERT_TRUE(act(BattleAction::makeDefend(battle()->battleActiveUnit()), PlayerColor(0)));
	EXPECT_EQ(reduction(id), 0);
}
INSTANTIATE_TEST_SUITE_P(SpellOrderDoctrine, GraveDustHeroContinuationTest, ::testing::Values(0, 1, 2));
}
