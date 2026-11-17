/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"

TEST_F(ForecastNativeAdapterTest, PassiveGenieCastingFactsDoNotDisableOrdinaryOwnedQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:masterGenie"));
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto genie = CreatureID(CreatureID::decode("core:masterGenie"));
	const battle::Unit * actor = nullptr, * target = nullptr;
	for(const auto * unit : parent->battleGetAllUnits(false))
		(unit->creatureId() == genie ? actor : target) = unit;
	ASSERT_NE(actor, nullptr); ASSERT_NE(target, nullptr);
	const auto * source = dynamic_cast<const battle::CUnitState *>(actor);
	ASSERT_NE(source, nullptr);
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::RANDOM_SPELLCASTER));
	EXPECT_EQ(actor->valOfBonuses(BonusType::CREATURE_ENCHANT_POWER), 5);
	EXPECT_EQ(source->casts.total(), 3);
	EXPECT_EQ(source->casts.available(), 3);
	// HATE remains a native pair-dependent family. Query ownership does not
	// claim support from the optional independent physical facts kernel.
	EXPECT_EQ(parent->preparedUnitDamageFacts(*actor, false), nullptr);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	BattleAttackInfo actual(actor, target, 0, false);
	BattleAttackInfo expected(reference->battleGetUnitByID(actor->unitId()),
		reference->battleGetUnitByID(target->unitId()), 0, false);
	const auto left = parent->calculateDmgRange(actual), right = reference->calculateDmgRange(expected);
	EXPECT_EQ(left.damage.min, right.damage.min); EXPECT_EQ(left.damage.max, right.damage.max);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
