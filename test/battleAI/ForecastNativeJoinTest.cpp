/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/spells/CSpell.h"

TEST_F(ForecastNativeAdapterTest, StableNativeObservationSurvivesLightForkAndPureCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto source = parent->getForUpdate(id);
	Bonus extra; extra.type = BonusType::ADDITIONAL_RETALIATION; extra.val = 1;
	extra.source = BonusSource::SPELL_EFFECT; extra.sid = BonusSourceID(SpellID(SpellID::HASTE));
	extra.duration = BonusDuration::N_TURNS; extra.turnsRemain = 2;
	source->addUnitBonus({extra});
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	EXPECT_FALSE(source->isBonusCacheable());
	ASSERT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), 0);
	ASSERT_EQ(source->counterAttacks.total(), 2);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	ASSERT_TRUE(child->forecastState()->readView(id));
	EXPECT_EQ(child->forecastState()->readView(id)->valuesOwner()->retaliationCache(), 2);
	int64_t damage = 1;
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *source, damage, battle::DamageProvenance::OTHER, nullptr));
	EXPECT_EQ(parent->forecastState()->unit(id)->nativeValues->retaliationCache(), 2);
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), 2);
	parent->removeUnitBonus(id, {extra});
	EXPECT_EQ(source->counterAttacks.available(), 2); // Native observed allowance lasts until reset.
	EXPECT_TRUE(child->battleGetUnitByID(id)->hasBonusOfType(BonusType::ADDITIONAL_RETALIATION));
	EXPECT_FALSE(parent->battleGetUnitByID(id)->hasBonusOfType(BonusType::ADDITIONAL_RETALIATION));
	const auto * childState = dynamic_cast<const battle::CUnitState *>(child->battleGetUnitByID(id));
	ASSERT_NE(childState, nullptr); EXPECT_EQ(childState->counterAttacks.available(), 2);
}

TEST_F(ForecastNativeAdapterTest, NativeMovementPublishesEntangleExpiryOrderBreakAndOwnedPositionTogether)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, true));
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	const auto old = parent->battleGetUnitByID(id)->getPosition();
	Bonus roots; roots.type = BonusType::BIND_EFFECT; roots.val = 1;
	roots.source = BonusSource::SPELL_EFFECT;
	roots.sid = BonusSourceID(SpellID(SpellID::decode("new-horizons:entangle")));
	roots.duration = BonusDuration::N_TURNS; roots.turnsRemain = 2;
	HeroOrderState hold; hold.command = HeroCommand::HOLD_THE_LINE;
	hold.issuedRound = parent->battleGetRound(); hold.anchors.push_back({id, old.toInt()});
	parent->getForUpdate(id)->addUnitBonus({roots});
	parent->setHeroOrderStates(BattleSide::ATTACKER, {hold});
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	reference->useReferenceForecast();
	reference->nextRound();
	reference->getForUpdate(id)->addUnitBonus({roots});
	reference->setHeroOrderStates(BattleSide::ATTACKER, {hold});
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto * stable = parent->battleGetUnitByID(id);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	parent->moveUnit(id, BattleHex(6, 5)); reference->moveUnit(id, BattleHex(6, 5));
	EXPECT_EQ(parent->battleGetUnitByID(id), stable);
	EXPECT_EQ(parent->forecastState()->unit(id)->position, BattleHex(6, 5));
	EXPECT_EQ(parent->forecastState()->readView(id)->unit().getPosition(), BattleHex(6, 5));
	EXPECT_EQ(parent->getForUpdate(id)->save(), reference->getForUpdate(id)->save());
	EXPECT_FALSE(parent->battleGetUnitByID(id)->hasBonusOfType(BonusType::BIND_EFFECT));
	ASSERT_TRUE(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE),
		reference->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	EXPECT_TRUE(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)->containsHoldBroken(id));
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
}
