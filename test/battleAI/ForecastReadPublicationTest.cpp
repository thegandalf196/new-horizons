/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
namespace
{
JsonNode fullRead(const battle::CUnitState & source)
{
	// Retaliation observations precede capture on BOTH actual/native baselines.
	(void)source.counterAttacks.total();
	return source.acquireState()->save();
}
void expectRead(const forecast::State & state, uint32_t id, const battle::CUnitState & expected)
{
	const auto * delta = state.unit(id);
	ASSERT_NE(delta, nullptr); ASSERT_TRUE(delta->readView); ASSERT_TRUE(delta->nativeValues);
	const auto & actual = delta->readView->unit();
	EXPECT_EQ(fullRead(actual), fullRead(expected));
	EXPECT_EQ(actual.shots.available(), expected.shots.available());
	EXPECT_EQ(actual.counterAttacks.available(), expected.counterAttacks.available());
	EXPECT_EQ(actual.getMovementRange(), expected.getMovementRange());
	EXPECT_EQ(actual.getInitiative(), expected.getInitiative());
	EXPECT_EQ(actual.getAttack(false), expected.getAttack(false));
	EXPECT_EQ(actual.getAttack(true), expected.getAttack(true));
	EXPECT_EQ(actual.getMaxDamage(true), expected.getMaxDamage(true));
	EXPECT_EQ(actual.isShooter(), expected.isShooter());
	EXPECT_EQ(actual.battleQueuePhase(0), expected.battleQueuePhase(0));
	EXPECT_EQ(delta->nativeValues->status().count, actual.getCount());
	EXPECT_EQ(delta->queries->binding().count, actual.getCount());
	EXPECT_TRUE(delta->currentModifiers->ownsQueryFrame(*delta->queries));
	const auto proof = forecast::NativeReadView::querySource(actual);
	ASSERT_TRUE(proof);
	EXPECT_EQ(proof->modifiers, delta->currentModifiers);
	EXPECT_EQ(proof->queries, delta->queries);
}
}
TEST_F(ForecastNativeAdapterTest, PublishedPureCasualtyAndTemporaryResurrectionOwnCompleteNativeValues)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	auto actual = child->getForUpdate(id), expected = reference->getForUpdate(id);
	const auto parentBefore = fullRead(parent->forecastState()->unit(id)->readView->unit());
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto * stable = actual.get();
	int64_t damage = actual->getMaxHealth() * 3 + 1, nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::SPELL);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actual, damage, battle::DamageProvenance::SPELL, nullptr));
	EXPECT_EQ(damage, nativeDamage);
	expectRead(*child->forecastState(), id, *expected);
	int64_t healing = actual->getMaxHealth() * 2, nativeHealing = healing;
	expected->heal(nativeHealing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_TRUE(forecast::NativeAdapter::heal(*child, *actual, healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE));
	EXPECT_EQ(healing, nativeHealing);
	expectRead(*child->forecastState(), id, *expected);
	EXPECT_EQ(child->getForUpdate(id).get(), stable);
	EXPECT_EQ(fullRead(parent->forecastState()->unit(id)->readView->unit()), parentBefore);
	EXPECT_EQ(fullRead(sibling->forecastState()->unit(id)->readView->unit()), parentBefore);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, FocusExpiryThenCasualtyPublishesCurrentProgramAndFullNativeReader)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto root = parent->getForUpdate(id);
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actual = child->getForUpdate(id), expected = reference->getForUpdate(id);
	const auto initialProgram = child->forecastState()->unit(id)->currentModifiers;
	for(int shot = 0; shot < 3; ++shot)
	{
		focusMagicAI::afterAttack(*actual, true, false, true, true, child.get());
		focusMagicAI::afterAttack(*expected, true, false, true);
		expectRead(*child->forecastState(), id, *expected);
	}
	EXPECT_NE(child->forecastState()->unit(id)->currentModifiers, initialProgram);
	EXPECT_EQ(newHorizonsFocusMagic::charges(child->forecastState()->unit(id)->readView->unit()), 0);
	const auto finalProgram = child->forecastState()->unit(id)->currentModifiers;
	int64_t damage = actual->getMaxHealth() + 1, nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actual, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	expectRead(*child->forecastState(), id, *expected);
	EXPECT_EQ(child->forecastState()->unit(id)->currentModifiers, finalProgram);
	EXPECT_EQ(child->forecastState()->unit(id)->queries->binding().count, expected->getCount());
}
TEST_F(ForecastNativeAdapterTest, CartDeathRefreshesOrderedContextAndNativeShotValuesForAllReaders)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	uint32_t archer = 0, cart = 0;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::ATTACKER)
		{
			if(unit->isAmmoCart()) cart = unit->unitId();
			else archer = unit->unitId();
		}
	ASSERT_NE(archer, cart);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	const auto oldReader = parent->forecastState()->unit(archer)->readView;
	EXPECT_FALSE(oldReader->unit().shots.isLimited());
	auto actualCart = child->getForUpdate(cart), nativeCart = reference->getForUpdate(cart);
	int64_t damage = actualCart->getAvailableHealth(), nativeDamage = damage;
	nativeCart->damage(nativeDamage, false, battle::DamageProvenance::SPELL);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actualCart, damage, battle::DamageProvenance::SPELL, nullptr));
	for(const auto id : child->forecastState()->orderedIds())
		expectRead(*child->forecastState(), id, *reference->getForUpdate(id));
	EXPECT_TRUE(child->forecastState()->unit(archer)->readView->unit().shots.isLimited());
	EXPECT_FALSE(oldReader->unit().shots.isLimited());
	EXPECT_GT(child->forecastState()->unit(archer)->readView->environmentOwner()->revision,
		oldReader->environmentOwner()->revision);
}
TEST_F(ForecastNativeAdapterTest, IncompleteContextRefreshRefusesBeforeAnyPublishedStateChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto draft = *parent->forecastState();
	const auto before = draft;
	const auto id = draft.orderedIds().front();
	const auto mapBefore = parent->battleGetUnitByID(id);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	EXPECT_THROW(forecast::NativeAdapter::refreshReadViews(draft, *parent, std::span(&id, 1), true), std::invalid_argument);
	EXPECT_TRUE(draft.sameSnapshot(before));
	EXPECT_EQ(parent->battleGetUnitByID(id), mapBefore);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
}

TEST_F(ForecastNativeAdapterTest, OwnedHateQueriesRemainValidWhenPhysicalPairFactsAreUnavailable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto root = parent->getForUpdate(id);
	Bonus hate;
	hate.type = BonusType::HATE; hate.subtype = BonusSubtypeID(root->creatureId()); hate.val = 25;
	hate.source = BonusSource::CREATURE_ABILITY;
	root->addUnitBonus({hate});
	ASSERT_FALSE(nativeDamage::prepareUnit(*root, false));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actual = child->getForUpdate(id), expected = reference->getForUpdate(id);
	int64_t damage = actual->getMaxHealth() + 1, nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::SPELL);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actual, damage, battle::DamageProvenance::SPELL, nullptr));
	const auto * value = child->forecastState()->unit(id);
	ASSERT_TRUE(value->queries); EXPECT_FALSE(value->numeric[0]); EXPECT_FALSE(value->numeric[1]);
	EXPECT_EQ(value->readView->unit().valOfBonuses(BonusType::HATE, root->creatureId()),
		expected->valOfBonuses(BonusType::HATE, root->creatureId()));
	expectRead(*child->forecastState(), id, *expected);
}
