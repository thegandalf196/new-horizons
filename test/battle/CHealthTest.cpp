/*
 * CHealthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "mock/mock_battle_Unit.h"
#include "mock/mock_BonusBearer.h"
#include "../../lib/battle/CUnitState.h"

using namespace testing;
using namespace battle;

static const int32_t UNIT_HEALTH = 123;
static const int32_t UNIT_AMOUNT = 300;

class HealthTest : public Test
{
public:
	UnitMock mock;
	BonusBearerMock bonusMock;
	HealthTest() : health(&mock)
	{}

	void setDefaultExpectations()
	{
		EXPECT_CALL(mock, getAllBonuses(_, _)).WillRepeatedly(Invoke(&bonusMock, &BonusBearerMock::getAllBonuses));
		EXPECT_CALL(mock, getTreeVersion()).WillRepeatedly(Return(1));

		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, UNIT_HEALTH, BonusSourceID()));

		EXPECT_CALL(mock, unitBaseAmount()).WillRepeatedly(Return(UNIT_AMOUNT));
	}
	CHealth health;
};

static void checkTotal(const CHealth & health, const UnitMock & mock)
{
	EXPECT_EQ(health.total(), mock.getMaxHealth() * mock.unitBaseAmount());
}

static void checkEmptyHealth(const CHealth & health, const UnitMock  & mock)
{
	checkTotal(health, mock);
	EXPECT_EQ(health.getCount(), 0);
	EXPECT_EQ(health.getFirstHPleft(), 0);
	EXPECT_EQ(health.getResurrected(), 0);
	EXPECT_EQ(health.getUnusableRemains(), 0);
	EXPECT_EQ(health.available(), 0);
}

static void checkFullHealth(const CHealth & health, const UnitMock  & mock)
{
	checkTotal(health, mock);
	EXPECT_EQ(health.getCount(), mock.unitBaseAmount());
	EXPECT_EQ(health.getFirstHPleft(), mock.getMaxHealth());
	EXPECT_EQ(health.getResurrected(), 0);
	EXPECT_EQ(health.getUnusableRemains(), 0);
	EXPECT_EQ(health.available(), mock.getMaxHealth() * mock.unitBaseAmount());
}

static void checkDamage(CHealth & health, const int64_t initialDamage, const int64_t expectedDamage)
{
	int64_t damage = initialDamage;
	health.damage(damage);
	EXPECT_EQ(damage, expectedDamage);
}

static void checkNormalDamage(CHealth & health, const int64_t initialDamage)
{
	checkDamage(health, initialDamage, initialDamage);
}

static void checkNoDamage(CHealth & health, const int64_t initialDamage)
{
	checkDamage(health, initialDamage, 0);
}

static void checkHeal(CHealth & health, EHealLevel level, EHealPower power, const int64_t initialHeal, const int64_t expectedHeal)
{
	int64_t heal = initialHeal;
	health.heal(heal, level, power);
	EXPECT_EQ(heal, expectedHeal);
}

TEST_F(HealthTest, empty)
{
	setDefaultExpectations();

	checkEmptyHealth(health, mock);

	health.init();
	checkFullHealth(health, mock);

	health.reset();
	checkEmptyHealth(health, mock);
}


TEST_F(HealthTest, damage)
{
	setDefaultExpectations();

	health.init();

	checkNormalDamage(health, 0);
	checkFullHealth(health, mock);

	checkNormalDamage(health, mock.getMaxHealth() - 1);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), 1);
	EXPECT_EQ(health.getResurrected(), 0);

	checkNormalDamage(health, 1);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	checkNormalDamage(health, UNIT_HEALTH * (UNIT_AMOUNT - 1));
	checkEmptyHealth(health, mock);

	checkNoDamage(health, 1337);
	checkEmptyHealth(health, mock);
}

TEST_F(HealthTest, heal)
{
	setDefaultExpectations();

	health.init();

	checkNormalDamage(health, 99);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH-99);
	EXPECT_EQ(health.getResurrected(), 0);

	checkHeal(health, EHealLevel::HEAL, EHealPower::PERMANENT, 9, 9);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH-90);
	EXPECT_EQ(health.getResurrected(), 0);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE, 40, 40);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH-50);
	EXPECT_EQ(health.getResurrected(), 0);

	checkHeal(health, EHealLevel::OVERHEAL, EHealPower::PERMANENT, 50, 50);
	checkFullHealth(health, mock);
}

TEST_F(HealthTest, resurrectOneBattle)
{
	setDefaultExpectations();
	health.init();

	checkNormalDamage(health, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE, UNIT_HEALTH, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 1);

	checkNormalDamage(health, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	health.init();

	checkNormalDamage(health, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	health.takeResurrected();
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	health.init();

	checkNormalDamage(health, UNIT_HEALTH * UNIT_AMOUNT);
	checkEmptyHealth(health, mock);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE, UNIT_HEALTH * UNIT_AMOUNT, UNIT_HEALTH * UNIT_AMOUNT);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), UNIT_AMOUNT);

	health.takeResurrected();
	checkEmptyHealth(health, mock);
}

TEST_F(HealthTest, resurrectPermanent)
{
	setDefaultExpectations();
	health.init();

	checkNormalDamage(health, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::PERMANENT, UNIT_HEALTH, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	checkNormalDamage(health, UNIT_HEALTH);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
	EXPECT_EQ(health.getFirstHPleft(), UNIT_HEALTH);
	EXPECT_EQ(health.getResurrected(), 0);

	health.init();

	checkNormalDamage(health, UNIT_HEALTH * UNIT_AMOUNT);
	checkEmptyHealth(health, mock);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::PERMANENT, UNIT_HEALTH * UNIT_AMOUNT, UNIT_HEALTH * UNIT_AMOUNT);
	checkFullHealth(health, mock);

	health.takeResurrected();
	checkFullHealth(health, mock);
}

TEST_F(HealthTest, singleUnitStack)
{
	//related to issue 2612

	//one Titan

	EXPECT_CALL(mock, getAllBonuses(_, _)).WillRepeatedly(Invoke(&bonusMock, &BonusBearerMock::getAllBonuses));
	EXPECT_CALL(mock, getTreeVersion()).WillRepeatedly(Return(1));

	bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 300, BonusSourceID()));

	EXPECT_CALL(mock, unitBaseAmount()).WillRepeatedly(Return(1));

	health.init();

	checkDamage(health, 1000, 300);
	checkEmptyHealth(health, mock);

	checkHeal(health, EHealLevel::RESURRECT, EHealPower::PERMANENT, 300, 300);
	checkFullHealth(health, mock);
}

TEST_F(HealthTest, destroyRemainsOnlyMarksCasualtiesAndBlocksResurrection)
{
	setDefaultExpectations();
	health.init();

	int64_t damage = UNIT_HEALTH * 2;
	health.damage(damage, true);
	EXPECT_EQ(damage, UNIT_HEALTH * 2);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 2);
	EXPECT_EQ(health.getUnusableRemains(), 2);

	// Damage to a surviving creature is not a destroyed remain.
	damage = 1;
	health.damage(damage, true);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 2);
	EXPECT_EQ(health.getUnusableRemains(), 2);

	// Resurrection may repair the surviving creature's missing hit point, but
	// cannot restore either casualty recorded by the special hit.
	int64_t heal = UNIT_HEALTH * 2;
	auto info = health.heal(heal, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(heal, 1);
	EXPECT_EQ(info.resurrectedCount, 0);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 2);
	EXPECT_EQ(health.getUnusableRemains(), 2);
}

TEST_F(HealthTest, ordinaryCasualtiesRemainUsableAfterLethalDestroyRemainsDamage)
{
	setDefaultExpectations();
	health.init();

	int64_t ordinaryDamage = UNIT_HEALTH * (UNIT_AMOUNT - 2);
	health.damage(ordinaryDamage);
	ASSERT_EQ(health.getCount(), 2);

	int64_t destroyingDamage = UNIT_HEALTH * 2;
	health.damage(destroyingDamage, true);
	ASSERT_EQ(health.getCount(), 0);
	EXPECT_EQ(health.getUnusableRemains(), 2);

	int64_t resurrection = UNIT_HEALTH * UNIT_AMOUNT;
	const auto info = health.heal(resurrection, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(info.resurrectedCount, UNIT_AMOUNT - 2);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 2);
	EXPECT_EQ(health.getUnusableRemains(), 2);
}
