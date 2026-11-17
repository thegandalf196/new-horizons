/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
namespace forecastTerrainIntegrationTest
{
class ForecastTerrainNativeIntegrationTest : public ForecastNativeAdapterTest {};
TEST_F(ForecastTerrainNativeIntegrationTest, OrdinaryNativeAndNonNativeGrassAdmitAndKeepNativeFactsAfterCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:gnoll"));
	ASSERT_EQ(parent->getBattle()->getTerrainType(), TerrainId(ETerrainId::GRASS));
	const auto before = gameState()->saveToMemory();
	const auto random = projectedHitTest::serialized(*handler->randomizer);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	ASSERT_NE(parent->forecastState(), nullptr);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	for(const auto id : parent->forecastState()->orderedIds())
	{
		auto unit = branch->getForUpdate(id);
		auto expected = reference->getForUpdate(id);
		const auto nativeRows = unit->getAllBonuses(Selector::sourceType()(BonusSource::TERRAIN_NATIVE));
		EXPECT_EQ(nativeRows->size(), unit->unitSide() == BattleSide::DEFENDER ? 3u : 0u);
		EXPECT_EQ(unit->getMovementRange(), expected->getMovementRange());
		EXPECT_EQ(unit->getAttack(false), expected->getAttack(false));
		EXPECT_EQ(unit->getDefense(false), expected->getDefense(false));
		const auto parentHealth = parent->battleGetUnitByID(id)->getAvailableHealth();
		int64_t damage = unit->getFirstHPleft(), nativeDamage = damage;
		expected->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		ASSERT_TRUE(forecast::NativeAdapter::damage(*branch, *unit, damage,
			battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
		EXPECT_EQ(damage, nativeDamage);
		EXPECT_EQ(unit->getMovementRange(), expected->getMovementRange());
		EXPECT_EQ(unit->getAttack(false), expected->getAttack(false));
		EXPECT_EQ(unit->getDefense(false), expected->getDefense(false));
		EXPECT_EQ(parent->battleGetUnitByID(id)->getAvailableHealth(), parentHealth);
	}
	const auto actorId = branch->forecastState()->orderedIds().front();
	const auto * actor = branch->battleGetUnitByID(actorId);
	const auto reachability = branch->getReachability(actor);
	const auto legal = branch->battleGetAvailableHexes(reachability, actor, true);
	const auto destination = std::ranges::find_if(legal, [&](BattleHex hex) { return hex != actor->getPosition(); });
	ASSERT_NE(destination, legal.end());
	branch->moveUnit(actorId, *destination);
	reference->moveUnit(actorId, *destination);
	const auto * moved = branch->battleGetUnitByID(actorId);
	const auto * nativeMoved = reference->battleGetUnitByID(actorId);
	EXPECT_EQ(moved->getPosition(), nativeMoved->getPosition());
	EXPECT_EQ(moved->getMovementRange(), nativeMoved->getMovementRange());
	EXPECT_EQ(moved->getAttack(false), nativeMoved->getAttack(false));
	EXPECT_EQ(moved->getDefense(false), nativeMoved->getDefense(false));
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), random);
}
}
