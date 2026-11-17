/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_battle_Unit.h"
#include "../../lib/battle/CHealth.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/JsonSerializer.h"

#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
using namespace battle;

class HealthOwner final : public testing::NiceMock<UnitMock>
{
public:
	int32_t originalCount = 8;
	uint32_t maximum = 100;
	bool forbidReads = false;
	mutable int reads = 0;

	int32_t unitBaseAmount() const override
	{
		if(forbidReads)
			throw std::logic_error("Forecast read the live health owner");
		++reads;
		return originalCount;
	}
	uint32_t getMaxHealth() const override
	{
		if(forbidReads)
			throw std::logic_error("Forecast read the live health owner");
		++reads;
		return maximum;
	}
};

JsonNode ledger(const CHealth & health)
{
	CHealth copy(health);
	JsonNode result;
	JsonSerializer serializer(nullptr, result);
	copy.serializeJson(serializer);
	return result;
}

void expectSame(const CHealth & native, const CHealth & value)
{
	EXPECT_EQ(ledger(native), ledger(value)); // All cohort/provenance fields, not just HP.
	EXPECT_EQ(native.available(), value.available());
	EXPECT_EQ(native.total(), value.total());
	EXPECT_EQ(native.getCount(), value.getCount());
}

template<typename Operation>
void applyBoth(CHealth & native, CHealth & value, Operation operation)
{
	operation(native);
	operation(value);
	expectSame(native, value);
}

void hit(CHealth & health, int64_t amount, DamageProvenance cause = DamageProvenance::SPELL,
	bool destroyRemains = false, bool bypassTemporary = false)
{
	health.damage(amount, destroyRemains, bypassTemporary, cause);
}

void restore(CHealth & health, int64_t amount, EHealLevel level, EHealPower power)
{
	const auto result = health.heal(amount, level, power);
	EXPECT_EQ(result.healedHealthPoints, amount);
}
}

TEST(CHealthValueContextTest, DamageHealingTemporaryResurrectionAndRemainsMatchNative)
{
	HealthOwner owner;
	CHealth native(&owner);
	native.init();
	auto value = CHealth::detached(native, native.valueContext());
	ASSERT_TRUE(value.isDetached());
	EXPECT_FALSE(native.isDetached());
	expectSame(native, value);
	applyBoth(native, value, [](CHealth & h) { h.addTemporaryHitPoints(50); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 175); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 20, EHealLevel::HEAL, EHealPower::PERMANENT); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 110, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 220, DamageProvenance::PHYSICAL_CREATURE, true); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 1000, EHealLevel::RESURRECT, EHealPower::PERMANENT); });
	auto recaptured = CHealth::detached(native, native.valueContext());
	expectSame(native, recaptured);
	applyBoth(native, value, [](CHealth & h) { h.takeResurrected(); });
}

TEST(CHealthValueContextTest, CapacityChangeRegenerationAndTemporaryGrowthMatchNative)
{
	HealthOwner owner;
	CHealth native(&owner);
	native.init();
	hit(native, 35);
	auto value = CHealth::detached(native, native.valueContext());
	applyBoth(native, value, [](CHealth & h) { h.preserveCapacityHealth(); });
	owner.maximum = 150;
	value.updateDetachedContext({8, 150});
	applyBoth(native, value, [](CHealth & h) { h.normalizeCapacityHealth(true); });
	EXPECT_EQ(native.capacityRegenerationProjectedHeal(17), value.capacityRegenerationProjectedHeal(17));
	applyBoth(native, value, [](CHealth & h) { h.consumeCapacityRegeneration(17); });
	applyBoth(native, value, [](CHealth & h) { EXPECT_TRUE(h.appendTemporaryCreatures(2)); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 260); });
	owner.maximum = 70;
	value.updateDetachedContext({8, 70});
	applyBoth(native, value, [](CHealth & h) { h.normalizeCapacityHealth(true); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 90, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE); });
	auto recaptured = CHealth::detached(native, native.valueContext());
	expectSame(native, recaptured);
	applyBoth(native, value, [](CHealth & h) { h.takeResurrected(); });
}

TEST(CHealthValueContextTest, BattleFormAndShadowGiftUseNativeLedger)
{
	HealthOwner owner;
	CHealth native(&owner);
	native.init();
	hit(native, 210);
	auto value = CHealth::detached(native, native.valueContext());
	applyBoth(native, value, [](CHealth & h) { h.preserveBattleFormProvenance(100); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 75, DamageProvenance::PHYSICAL_CREATURE); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 100, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE); });
	applyBoth(native, value, [](CHealth & h) { h.releaseBattleFormProvenance(true); });
	applyBoth(native, value, [](CHealth & h) { h.addTemporaryHitPoints(80); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 25, DamageProvenance::OTHER, false, true); });
	applyBoth(native, value, [](CHealth & h) { h.addShadowGiftMaximumHealthLoss(25); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 500, EHealLevel::RESURRECT, EHealPower::PERMANENT); });
}

TEST(CHealthValueContextTest, CopiesOutliveOwnerAndMutationsNeverReadLiveState)
{
	auto owner = std::make_unique<HealthOwner>();
	CHealth native(owner.get());
	native.init();
	hit(native, 230);
	auto parent = CHealth::detached(native, native.valueContext());
	const auto before = ledger(parent);
	owner->forbidReads = true;
	auto child(parent);
	EXPECT_NO_THROW(hit(child, 95));
	EXPECT_EQ(ledger(parent), before);
	EXPECT_NE(ledger(child), before);
	owner.reset();
	auto grandchild(child);
	EXPECT_NO_THROW(restore(grandchild, 100, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE));
	EXPECT_NO_THROW(grandchild.takeResurrected());
	EXPECT_NO_THROW(ledger(grandchild));
	EXPECT_EQ(parent.valueContext(), (CHealthValueContext{8, 100}));
}

TEST(CHealthValueContextTest, AssignmentAndPreparedSwapRetainReceivingContext)
{
	HealthOwner first, second;
	second.maximum = 120;
	CHealth native(&first), other(&second);
	native.init();
	other.init();
	auto value = CHealth::detached(native, native.valueContext());
	auto alternative = CHealth::detached(other, other.valueContext());
	value = alternative;
	EXPECT_EQ(value.valueContext(), (CHealthValueContext{8, 100}));
	EXPECT_TRUE(value.isDetached());
	native = alternative;
	EXPECT_FALSE(native.isDetached());
	EXPECT_EQ(native.valueContext(), (CHealthValueContext{8, 100}));
	value.swapPreparedContents(alternative);
	EXPECT_EQ(value.valueContext(), (CHealthValueContext{8, 100}));
	EXPECT_EQ(alternative.valueContext(), (CHealthValueContext{8, 120}));
}

TEST(CHealthValueContextTest, InvalidOrReinterpretedContextRejectsBeforeMutation)
{
	HealthOwner owner;
	CHealth native(&owner);
	native.init();
	auto value = CHealth::detached(native, native.valueContext());
	const auto before = ledger(value);
	EXPECT_THROW(CHealth::detached(native, CHealthValueContext{-1, 100}), std::invalid_argument);
	EXPECT_THROW(CHealth::detached(native, CHealthValueContext{8, 0}), std::invalid_argument);
	EXPECT_THROW(CHealth::detached(native, CHealthValueContext{8, 101}), std::invalid_argument);
	EXPECT_THROW(value.updateDetachedContext(CHealthValueContext{8, -1}), std::invalid_argument);
	EXPECT_THROW(value.updateDetachedContext(CHealthValueContext{9, 100}), std::invalid_argument);
	EXPECT_THROW(native.updateDetachedContext(CHealthValueContext{8, 100}), std::logic_error);
	EXPECT_EQ(ledger(value), before);
	EXPECT_EQ(value.valueContext(), (CHealthValueContext{8, 100}));
	owner.maximum = std::numeric_limits<uint32_t>::max();
	EXPECT_THROW(CHealth::detached(native, CHealthValueContext{8, 100}), std::runtime_error);
}

TEST(CHealthValueContextTest, ExtremeLegalCountAndTemporaryGrowthRejectionMatchNative)
{
	HealthOwner owner;
	owner.originalCount = std::numeric_limits<int32_t>::max();
	owner.maximum = std::numeric_limits<int32_t>::max();
	CHealth native(&owner);
	native.init();
	auto value = CHealth::detached(native, native.valueContext());
	expectSame(native, value);
	applyBoth(native, value, [](CHealth & h) { EXPECT_FALSE(h.appendTemporaryCreatures(1)); });
	applyBoth(native, value, [](CHealth & h) { hit(h, std::numeric_limits<int32_t>::max()); });
	applyBoth(native, value, [](CHealth & h) { restore(h, 1, EHealLevel::RESURRECT, EHealPower::PERMANENT); });
}

TEST(CHealthValueContextTest, BattleFormRepartitionRetainsTotalAndTemporaryPools)
{
	HealthOwner owner;
	CHealth native(&owner);
	native.init();
	hit(native, 210);
	native.addTemporaryHitPoints(55);
	auto value = CHealth::detached(native, native.valueContext());
	const auto originalTotal = native.total();
	const auto survivors = native.getCreatureHealthAvailable();
	owner.maximum = 50;
	value.updateDetachedContext({8, 50});
	applyBoth(native, value, [&](CHealth & h) { h.repartitionForBattleForm(50, originalTotal, survivors); });
	applyBoth(native, value, [](CHealth & h) { hit(h, 80); });
	auto recaptured = CHealth::detached(native, native.valueContext());
	expectSame(native, recaptured);
}
