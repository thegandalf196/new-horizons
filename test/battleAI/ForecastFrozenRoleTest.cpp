/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../lib/battle/NewHorizonsFrozen.h"

TEST_F(ForecastNativeAdapterTest, NativeIceFreezingTouchKeepsProbabilisticControlAndBothRngStreams)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:iceElemental"));
	// Select the authored Ice Elemental, independently of native roster ordering.
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	const auto defenders = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u); ASSERT_EQ(defenders.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:iceElemental")));
	ASSERT_EQ(defenders.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto actorId = attackers.front()->unitId();
	const auto targetId = defenders.front()->unitId();
	ASSERT_FALSE(parent->battleGetUnitByID(actorId)->isShooter());
	ASSERT_TRUE(newHorizonsFrozen::hasFreezingTouchRole(*parent->battleGetUnitByID(actorId), parent->getMagicRules()));
	// Keep a surviving defender: 100 current Ice Elementals can kill the entire Pikeman stack.
	auto actor = parent->getForUpdate(actorId);
	int64_t loss = actor->getAvailableHealth() - int64_t(10) * actor->getMaxHealth();
	ASSERT_GT(loss, 0);
	actor->damage(loss, false, battle::DamageProvenance::OTHER);
	ASSERT_EQ(actor->getCount(), 10);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	reference->useReferenceForecast();
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	ASSERT_GT(newHorizonsFrozen::chancePercent(parent->getMagicRules()), 0);
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
	for(const auto * preview : {&compact, &native})
	{
		const auto survivor = std::ranges::find_if(preview->affectedUnits, [targetId](const auto & unit)
		{ return unit->unitId() == targetId; });
		ASSERT_NE(survivor, preview->affectedUnits.end());
		ASSERT_TRUE((*survivor)->alive());
		EXPECT_LT((*survivor)->getAvailableHealth(), parent->battleGetUnitByID(targetId)->getAvailableHealth());
	}
	EXPECT_GT(compact.frozenControlValue, 0);
	ASSERT_NE(compact.attackerState, nullptr); ASSERT_NE(native.attackerState, nullptr);
	EXPECT_EQ(compact.attackerState->getAvailableHealth(), native.attackerState->getAvailableHealth());
	EXPECT_EQ(parent->battleGetUnitByID(actorId)->getAvailableHealth(), reference->battleGetUnitByID(actorId)->getAvailableHealth());
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*parent->battleGetUnitByID(targetId)));
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
