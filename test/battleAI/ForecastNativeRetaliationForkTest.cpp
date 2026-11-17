/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
TEST_F(ForecastNativeAdapterTest, ActualCompactSiblingReadonlyObservationDoesNotSurviveInUnobservedSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto root = parent->getForUpdate(id); root->counterAttacks.reset();
	Bonus extra; extra.type = BonusType::ADDITIONAL_RETALIATION; extra.val = 1;
	extra.source = BonusSource::SPELL_EFFECT; extra.sid = BonusSourceID(SpellID(SpellID::HASTE));
	extra.duration = BonusDuration::N_TURNS; extra.turnsRemain = 1;
	root->addUnitBonus({extra});
	const auto raw = battle::UnitStateValueSnapshot::retaliationCache(*root);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*root), raw);
	auto observed = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto unobserved = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto nativeObserved = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto nativeUnobserved = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *observed);
	forecast::NativeAdapter::forkExchange(*parent, *unobserved);
	auto expectedObserved = nativeObserved->getForUpdate(id);
	auto expectedUnobserved = nativeUnobserved->getForUpdate(id);
	EXPECT_EQ(observed->forecastState()->readView(id)->unit().counterAttacks.total(),
		expectedObserved->counterAttacks.total());
	EXPECT_EQ(unobserved->forecastState()->readView(id)->valuesOwner()->retaliationCache(), raw);
	EXPECT_NE(observed->forecastState()->readView(id)->workIdentity(),
		unobserved->forecastState()->readView(id)->workIdentity());
	expectedObserved->removeUnitBonus(Selector::type()(BonusType::ADDITIONAL_RETALIATION));
	expectedUnobserved->removeUnitBonus(Selector::type()(BonusType::ADDITIONAL_RETALIATION));
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*observed, id, {extra}, {}));
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*unobserved, id, {extra}, {}));
	EXPECT_EQ(observed->forecastState()->readView(id)->unit().counterAttacks.total(), expectedObserved->counterAttacks.total());
	EXPECT_EQ(unobserved->forecastState()->readView(id)->unit().counterAttacks.total(), expectedUnobserved->counterAttacks.total());
	EXPECT_GT(expectedObserved->counterAttacks.total(), expectedUnobserved->counterAttacks.total());
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*root), raw);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
