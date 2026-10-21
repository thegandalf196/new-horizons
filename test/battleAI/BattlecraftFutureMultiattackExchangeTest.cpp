/*
 * BattlecraftFutureMultiattackExchangeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattlecraftFutureBoundaryFixture.h"

class BattlecraftFutureMultiattackExchangeTest : public battlecraftTest::FutureBoundaryFixture {};

TEST_F(BattlecraftFutureMultiattackExchangeTest, ProductionOuterLoopDoesNotRepeatTheFullAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true, false, false, true));
	ASSERT_NO_FATAL_FAILURE(extraAttack());
	const auto liveBefore = snapshot(*battle());
	Result expected;
	{
		auto parent = simulation();
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto before = snapshot(*parent);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::DEFENDER);
		const auto * shooter = parent->battleGetUnitByID(opening->unitId());
		const auto * incoming = parent->battleGetUnitByID(actor->unitId());
		ASSERT_TRUE(parent->battleCanShootAction(shooter, incoming->getPosition()));
		const auto openingAttack = AttackPossibility::evaluate(
			BattleAttackInfo(shooter, incoming, 0, true), shooter->getPosition(), cache, parent);
		PotentialTargets targets(shooter, cache, parent);
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 1.0f, 1);
		evaluator.updateReachabilityMap(parent);
		const auto members = evaluator.getExchangeUnits(openingAttack, 0, targets, parent);
		ASSERT_EQ(members.units.size(), 2u);
		ASSERT_EQ(members.units.at(0).size(), 2u);
		ASSERT_EQ(members.units.at(0)[0]->unitId(), opening->unitId());
		ASSERT_EQ(members.units.at(0)[1]->unitId(), actor->unitId());
		ASSERT_TRUE(vstd::contains(members.shooters, shooter));
		ASSERT_TRUE(vstd::contains(members.melleeAccessible, parent->battleGetUnitByID(target->unitId())));
		ASSERT_TRUE(std::ranges::none_of(members.melleeAccessible, [&](const auto * unit)
			{ return unit->unitId() == reserve->unitId(); }));
		const auto actual = evaluator.evaluateExchange(openingAttack, 0, targets, cache, parent);
		{
			auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
			BattleExchangeVariant first;
			first.trackAttack(openingAttack, reference, cache);
			reference->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			ASSERT_EQ(AttackPossibility::getAttackCount(
				*reference->getForUpdate(actor->unitId()), false, *reference), 2);
			ASSERT_NO_FATAL_FAILURE(selected(reference, expected));
			ASSERT_EQ(expected.strikes.size(), 3u);
			EXPECT_EQ(expected.strikes[0], std::make_pair(target->unitId(), actor->unitId()));
			EXPECT_EQ(expected.strikes[1], std::make_pair(actor->unitId(), target->unitId()));
			EXPECT_EQ(expected.strikes[2], std::make_pair(actor->unitId(), target->unitId()));
			auto firstScore = first.getScore();
			// Existing one-round/two-queue scaling; the reference uses selected APs once each.
			EXPECT_FLOAT_EQ(actual, (firstScore.value() - expected.value) / 2.0f);
		}
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
	}
	// Every forecast provider has retired before authoritative execution.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(opening, actor)));
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
	ASSERT_NO_FATAL_FAILURE(native(expected));
	ASSERT_EQ(server.attacks.size(), 3u);
}
