/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../lib/battle/NewHorizonsFrozen.h"

TEST_F(ForecastNativeAdapterTest, NativeIceFreezingTouchKeepsProbabilisticControlAndBothRngStreams)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:iceElemental"));
	const auto actorId = parent->battleGetAllUnits(false).front()->unitId();
	ASSERT_TRUE(newHorizonsFrozen::hasFreezingTouchRole(*parent->battleGetUnitByID(actorId), parent->getMagicRules()));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	reference->useReferenceForecast();
	const auto targetId = parent->battleGetAllUnits(false).back()->unitId();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto evaluate = [&](const std::shared_ptr<HypotheticBattle> & board)
	{
		const auto * actor = board->battleGetUnitByID(actorId);
		const auto * victim = board->battleGetUnitByID(targetId);
		BattleAttackInfo attack(actor, victim, 1, false);
		DamageCache cache;
		return AttackPossibility::evaluate(attack, BattleHex(6, 5), cache, board);
	};
	const auto compact = evaluate(parent);
	const auto native = evaluate(reference);
	EXPECT_FLOAT_EQ(compact.attackValue(), native.attackValue());
	EXPECT_FLOAT_EQ(compact.frozenControlValue, native.frozenControlValue);
	EXPECT_GT(compact.frozenControlValue, 0);
	ASSERT_NE(compact.attackerState, nullptr); ASSERT_NE(native.attackerState, nullptr);
	EXPECT_EQ(compact.attackerState->getAvailableHealth(), native.attackerState->getAvailableHealth());
	EXPECT_EQ(parent->battleGetUnitByID(actorId)->getAvailableHealth(), reference->battleGetUnitByID(actorId)->getAvailableHealth());
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*parent->battleGetUnitByID(targetId)));
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
