/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/json/JsonNode.h"
#include <stdexcept>
namespace
{
using namespace ::testing;
using namespace battle;
class IncomingHealingTest : public Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock env;
	BonusBearerMock bonuses;
	CUnitStateDetached state{&info, &bonuses};
	void SetUp() override
	{
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(3));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor::NEUTRAL));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
		state.localInit(&env);
	}
	void reduction(int value)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_ACTIVATION,
			BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS, BonusSource::OTHER,
			value, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	}
};
TEST_F(IncomingHealingTest, RawAllocationIsReadOnlyAndReceiptScalesCappedBenefit)
{
	int64_t loss = 10;
	state.damage(loss);
	reduction(5000);
	const auto before = state.save();
	EXPECT_EQ(state.healingCapacity(1000, EHealLevel::HEAL, EHealPower::PERMANENT), 10);
	EXPECT_EQ(state.save(), before);
	int64_t request = 1000;
	const auto received = state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT);
	EXPECT_EQ(received.healedHealthPoints, 5);
	EXPECT_EQ(request, 5);
	EXPECT_EQ(received.resurrectedCount, 0);
	EXPECT_EQ(state.getAvailableHealth(), 295);
}
TEST_F(IncomingHealingTest, StrongestReductionDoesNotAddOrMultiplyAndOneHpSurvivesPartialReduction)
{
	int64_t loss = 1;
	state.damage(loss);
	reduction(2500);
	reduction(5000);
	int64_t request = 1000;
	EXPECT_EQ(state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 1);
	loss = 10;
	state.damage(loss);
	request = 1000;
	EXPECT_EQ(state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 5);
}
TEST_F(IncomingHealingTest, CasualtyReceiptPreservesTemporaryRestorationProvenance)
{
	int64_t loss = 130;
	state.damage(loss);
	reduction(5000);
	EXPECT_EQ(state.healingCapacity(1000, EHealLevel::HEAL, EHealPower::PERMANENT), 30);
	EXPECT_EQ(state.healingCapacity(1000, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE), 130);
	int64_t request = 1000;
	const auto received = state.heal(request, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(request, 65);
	EXPECT_EQ(received.healedHealthPoints, 65);
	EXPECT_EQ(received.resurrectedCount, 1);
	EXPECT_EQ(state.save()["state"]["health"]["resurrected"].Integer(), 1);
	EXPECT_EQ(state.getAvailableHealth(), 235);
}
TEST_F(IncomingHealingTest, DestroyedRemainsStayUnavailableBeforeReceiptReduction)
{
	int64_t loss = 130;
	state.damage(loss, true);
	ASSERT_EQ(state.getUnusableRemains(), 1);
	reduction(5000);
	EXPECT_EQ(state.healingCapacity(1000, EHealLevel::RESURRECT, EHealPower::PERMANENT), 30);
	int64_t request = 1000;
	const auto received = state.heal(request, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(received.healedHealthPoints, 15);
	EXPECT_EQ(received.resurrectedCount, 0);
	EXPECT_EQ(state.getUnusableRemains(), 1);
}
TEST_F(IncomingHealingTest, FullReductionDoesNotBlockNativeRecreationOrExistingGrowth)
{
	int64_t loss = 300;
	state.damage(loss);
	reduction(10'000);
	int64_t request = 300;
	EXPECT_EQ(state.heal(request, EHealLevel::RESURRECT, EHealPower::PERMANENT).healedHealthPoints, 0);
	request = 300;
	EXPECT_EQ(state.recreateAfterNativeRebirth(request).healedHealthPoints, 300);
	EXPECT_EQ(state.getCount(), 3);
	loss = 10;
	state.damage(loss);
	request = 100;
	EXPECT_EQ(state.heal(request, EHealLevel::OVERHEAL, EHealPower::ONE_BATTLE).healedHealthPoints, 100);
	EXPECT_EQ(state.getCount(), 4);
	EXPECT_EQ(state.getAvailableHealth(), 390);
}
TEST_F(IncomingHealingTest, NoModifierAndExplicitZeroKeepTheOriginalReceipt)
{
	int64_t loss = 10;
	state.damage(loss);
	const auto before = state.save();
	int64_t request = 1000;
	const auto ordinary = state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT);
	const auto healed = state.save();
	state.load(before);
	reduction(0);
	request = 1000;
	const auto zero = state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT);
	EXPECT_EQ(zero.healedHealthPoints, ordinary.healedHealthPoints);
	EXPECT_EQ(zero.resurrectedCount, ordinary.resurrectedCount);
	EXPECT_EQ(request, 10);
	EXPECT_EQ(state.save(), healed);
}
TEST_F(IncomingHealingTest, CapacityQueryKeepsIneligibleRecipientsAndInvalidHealPowerClosed)
{
	int64_t loss = 10;
	state.damage(loss);
	state.cloned = true;
	EXPECT_EQ(state.healingCapacity(100, EHealLevel::RESURRECT, EHealPower::PERMANENT), 0);
	state.cloned = false;
	EXPECT_EQ(state.healingCapacity(100, EHealLevel::HEAL, EHealPower::ONE_BATTLE), 0);
	state.summoned = true;
	ASSERT_NO_THROW(state.initializePhantomProfile(150, 3));
	ASSERT_EQ(state.getPhantomInitialIntegrity(), 150);
	EXPECT_EQ(state.healingCapacity(100, EHealLevel::HEAL, EHealPower::PERMANENT), 0);
}
TEST_F(IncomingHealingTest, TimeStopAndMalformedReductionDoNotMutateHealth)
{
	int64_t loss = 10;
	state.damage(loss);
	reduction(10'001);
	const auto before = state.save();
	int64_t request = 1000;
	EXPECT_THROW(state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT), std::invalid_argument);
	EXPECT_EQ(state.save(), before);
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(state.healingCapacity(1000, EHealLevel::HEAL, EHealPower::PERMANENT), 0);
	EXPECT_EQ(state.heal(request, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 0);
	EXPECT_EQ(request, 0);
}
TEST_F(IncomingHealingTest, HydraScalesAggregateOnceWithoutGivingOneBodyMoreThanItsAllowance)
{
	state.preserveCreatureHealthOnCapacityIncrease();
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACK_HEALTH, BonusSource::OTHER, 100, BonusSourceID()));
	state.normalizeCapacityHealth();
	reduction(5000);
	ASSERT_EQ(state.getMaxHealth(), 200);
	ASSERT_EQ(state.getAvailableHealth(), 300);
	const auto before = state.save();
	EXPECT_EQ(state.capacityRegenerationProjectedHeal(), 30);
	EXPECT_EQ(state.save(), before);
	EXPECT_EQ(state.consumeCapacityRegeneration(), 30);
	EXPECT_EQ(state.getCount(), 3);
	EXPECT_EQ(state.getFirstHPleft(), 120); // Ordinary aggregate HEAL30 would wrongly give this body130.
	EXPECT_EQ(state.getAvailableHealth(), 330);
	const auto ledger = state.save()["state"]["health"]["capacityHealthCohorts"];
	ASSERT_EQ(ledger.Vector().size(), 2u);
	EXPECT_EQ(ledger.Vector()[0]["hitPoints"].Integer(), 100);
	EXPECT_EQ(ledger.Vector()[1]["hitPoints"].Integer(), 110);
	EXPECT_EQ(ledger.Vector()[0]["count"].Integer(), 1);
	EXPECT_EQ(ledger.Vector()[1]["count"].Integer(), 1);
}
TEST_F(IncomingHealingTest, BlockedHydraConsumesTenthsWithoutCreatingDeferredHealingDebt)
{
	state.preserveCreatureHealthOnCapacityIncrease();
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACK_HEALTH, BonusSource::OTHER, 5, BonusSourceID()));
	state.normalizeCapacityHealth();
	reduction(10'000);
	ASSERT_EQ(state.getMaxHealth(), 105);
	const auto beforeHealth = state.save()["state"]["health"];
	EXPECT_EQ(state.consumeCapacityRegeneration(), 0);
	EXPECT_EQ(state.capacityRegenerationRemainderTenths, 5);
	EXPECT_EQ(state.consumeCapacityRegeneration(), 0);
	EXPECT_EQ(state.capacityRegenerationRemainderTenths, 0);
	EXPECT_EQ(state.save()["state"]["health"], beforeHealth);
	EXPECT_EQ(state.getAvailableHealth(), 300);
	EXPECT_EQ(state.getCount(), 3);
}
TEST_F(IncomingHealingTest, HydraNoModifierUsesExactExistingPerBodyOperation)
{
	state.preserveCreatureHealthOnCapacityIncrease();
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACK_HEALTH, BonusSource::OTHER, 100, BonusSourceID()));
	state.normalizeCapacityHealth();
	const auto before = state.save();
	EXPECT_EQ(state.consumeCapacityRegeneration(), 60);
	const auto oldResult = state.save();
	state.load(before);
	reduction(0);
	EXPECT_EQ(state.consumeCapacityRegeneration(), 60);
	EXPECT_EQ(state.save(), oldResult);
}
TEST_F(IncomingHealingTest, RegenerationMarksAreSpentBeforeReceiptAndNotReturnedWhenBlocked)
{
	int64_t loss = 10;
	state.damage(loss);
	state.regenerationPendingMicroHealth = 10'000'000;
	reduction(10'000);
	int64_t marked = state.consumeRegenerationMarks();
	ASSERT_EQ(marked, 10);
	EXPECT_EQ(state.heal(marked, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 0);
	EXPECT_EQ(state.regenerationPendingMicroHealth, 0);
	EXPECT_EQ(state.consumeRegenerationMarks(), 0);
}
}
