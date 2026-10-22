/*
 * DamageCalculationTimingRoutingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/CRandomGenerator.h"

namespace
{
class DamageCalculationTimingRoutingTest : public ProjectedTargetHitFixture
{
protected:
	void check(int luck)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto delta = luck - child->battleGetAttackLuck(incoming, braced, false);
		incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, delta, BonusSourceID()));
		resetChild();
		const auto actor = child->getForUpdate(incoming->unitId());
		const auto target = child->getForUpdate(braced->unitId());
		ASSERT_EQ(child->battleGetAttackLuck(actor.get(), target.get(), false), luck);
		BattleAttackInfo attack(actor.get(), target.get(), 0, false);
		ASSERT_FALSE(child->battleCanTriggerPerfectFortune(attack));
		// Ordinary materialization precedes the immutable comparison, as in
		// the maintained damage/forecast fixtures; exclude no serialized fields.
		for(const auto * unit : child->battleGetAllUnits(false))
			(void)child->getForUpdate(unit->unitId())->counterAttacks.total();
		const auto actorBefore = actor->save();
		const auto targetBefore = target->save();
		const auto serverBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto rngBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto expected = child->battleExpectedLuckDamage(attack);
		damageCalculationTiming::Sample sample;
		int64_t actual = 0;
		{
			damageCalculationTiming::Binding enabled(&sample);
			actual = child->battleExpectedLuckDamage(attack);
		}
		EXPECT_EQ(actual, expected);
		EXPECT_EQ(sample.initialCalls, 1u);
		EXPECT_EQ(sample.luckySecondCalls, luck > 0 ? 1u : 0u);
		EXPECT_EQ(sample.unluckySecondCalls, luck < 0 ? 1u : 0u);
		const auto calculations = luck == 0 ? 1u : 2u;
		EXPECT_EQ(sample.nativePayload.calls, calculations);
		EXPECT_EQ(sample.bonusPreparation.calls, calculations);
		EXPECT_EQ(sample.luaCall.calls, calculations);
		EXPECT_EQ(sample.suppressedStages, 0u);
		EXPECT_EQ(actor->save(), actorBefore);
		EXPECT_EQ(target->save(), targetBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverBefore);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rngBefore);
	}
};
}

TEST_F(DamageCalculationTimingRoutingTest, ZeroLuckHasOnlyInitialCalculation)
{
	ASSERT_NO_FATAL_FAILURE(check(0));
}

TEST_F(DamageCalculationTimingRoutingTest, PositiveLuckCountsSecondCalculation)
{
	ASSERT_NO_FATAL_FAILURE(check(3));
}

TEST_F(DamageCalculationTimingRoutingTest, NegativeLuckCountsSecondCalculation)
{
	ASSERT_NO_FATAL_FAILURE(check(-3));
}
