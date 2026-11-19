/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackProjectionTestAccess.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastAmmoCartAdmission.h"

TEST_F(ForecastNativeAdapterTest, CanonicalCartIntrinsicRemainsPassiveAndRejectsOverlayReplacement)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	std::shared_ptr<StackWithBonuses> cart, ordinary;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->isAmmoCart()) cart = parent->getForUpdate(unit->unitId());
		else ordinary = parent->getForUpdate(unit->unitId());
	ASSERT_TRUE(cart); ASSERT_TRUE(ordinary);
	const auto * intrinsic = forecast::ammoCartAdmission::intrinsic(*cart);
	ASSERT_NE(intrinsic, nullptr);
	// Preserve the native source identity before prepareExchange publishes the
	// detached owned creature view. Overlay removal refers to this original row.
	const auto & nativeLocal = cart->unitType()->getExportedBonusList();
	const auto nativeRow = std::ranges::find_if(nativeLocal, [&](const auto & value) { return value.get() == intrinsic; });
	ASSERT_NE(nativeRow, nativeLocal.end());
	const auto intrinsicRecord = *nativeRow;
	EXPECT_FALSE(cart->canMove());
	EXPECT_EQ(forecast::ammoCartAdmission::intrinsic(*ordinary), nullptr);
	const auto outside = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	EXPECT_FALSE(parent->forecastState()->supports(forecast::Event::HeroResource));
	for(int mutation = 0; mutation < 3; ++mutation)
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), visible);
		auto edited = branch->getForUpdate(cart->unitId());
		auto foreign = std::make_shared<Bonus>(*intrinsic);
		if(mutation == 0) StackWithBonusesTestAccess::removeIdentity(*edited, intrinsicRecord);
		if(mutation == 1) StackWithBonusesTestAccess::appendAdded(*edited, foreign);
		if(mutation == 2) StackWithBonusesTestAccess::appendUpdated(*edited, foreign);
		EXPECT_EQ(forecast::ammoCartAdmission::intrinsic(*edited), nullptr) << mutation;
		EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*branch)) << mutation;
		EXPECT_FALSE(branch->forecastState());
	}
	EXPECT_EQ(gameState()->saveToMemory(), outside);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, CanonicalCartRejectsEvenInactiveForeignRawNotActiveSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	std::shared_ptr<StackWithBonuses> cart;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->isAmmoCart()) cart = parent->getForUpdate(unit->unitId());
	ASSERT_TRUE(cart);
	const auto * intrinsic = forecast::ammoCartAdmission::intrinsic(*cart);
	ASSERT_NE(intrinsic, nullptr);
	const auto * original = dynamic_cast<const CStack *>(visible->battleGetUnitByID(cart->unitId()));
	ASSERT_NE(original, nullptr);
	auto foreign = std::make_shared<Bonus>(*intrinsic);
	foreign->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::REBIRTH);
	// This is a native source row, before selection/limitation can erase it.
	auto * writable = const_cast<CStack *>(original);
	writable->addNewBonus(foreign);
	EXPECT_EQ(forecast::ammoCartAdmission::intrinsic(*cart), nullptr);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*parent));
	EXPECT_FALSE(parent->forecastState());
	writable->removeBonus(foreign);
	EXPECT_EQ(forecast::ammoCartAdmission::intrinsic(*cart), intrinsic);
}

TEST(ForecastAmmoCartAdmission, DeferredNativeDescriptorAndExactPassiveDescriptorAreInert)
{
	Bonus native;
	native.type = BonusType::NOT_ACTIVE;
	native.source = BonusSource::CREATURE_ABILITY;
	native.sid = BonusSourceID(CreatureID(CreatureID::AMMO_CART));
	// Normal content loading queues type resolution; the earlier creature
	// loader's fromInnateBonus(NONE) has left this field absent.
	ASSERT_FALSE(native.abilityDescriptor);
	EXPECT_TRUE(forecast::ammoCartAdmission::isInertIntrinsic(native));
	native.abilityDescriptor = creatureAbility::fromInnateBonus(native);
	ASSERT_TRUE(native.abilityDescriptor);
	EXPECT_TRUE(forecast::ammoCartAdmission::isInertIntrinsic(native));
	auto foreign = std::make_shared<CreatureAbilityDescriptor>(*native.abilityDescriptor);
	foreign->categories = creatureAbility::HOSTILE_AFFLICTION;
	native.abilityDescriptor = foreign;
	EXPECT_FALSE(forecast::ammoCartAdmission::isInertIntrinsic(native));
	native.abilityDescriptor.reset();
	native.source = BonusSource::OTHER;
	EXPECT_FALSE(forecast::ammoCartAdmission::isInertIntrinsic(native));
	native.source = BonusSource::CREATURE_ABILITY;
	native.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::REBIRTH);
	EXPECT_FALSE(forecast::ammoCartAdmission::isInertIntrinsic(native));
}
