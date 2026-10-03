/*
 * CasualtyProvenanceTest.cpp, part of VCMI engine
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

namespace
{
constexpr int32_t UNIT_HEALTH = 123;
constexpr int32_t UNIT_AMOUNT = 10;

class CasualtyProvenanceTest : public Test
{
public:
	UnitMock unit;
	BonusBearerMock bonuses;
	CHealth health;

	CasualtyProvenanceTest()
		: health(&unit)
	{
		EXPECT_CALL(unit, getAllBonuses(_, _)).WillRepeatedly(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		EXPECT_CALL(unit, getTreeVersion()).WillRepeatedly(Return(1));
		EXPECT_CALL(unit, unitBaseAmount()).WillRepeatedly(Return(UNIT_AMOUNT));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
			BonusSource::CREATURE_ABILITY, UNIT_HEALTH, BonusSourceID()));
	}

	void damageUnits(const int32_t count, const DamageProvenance provenance)
	{
		int64_t damage = static_cast<int64_t>(UNIT_HEALTH) * count;
		health.damage(damage, false, false, provenance);
		EXPECT_EQ(damage, static_cast<int64_t>(UNIT_HEALTH) * count);
	}

	void overHealUnits(const int32_t count, const EHealPower power)
	{
		int64_t healing = static_cast<int64_t>(UNIT_HEALTH) * count;
		health.heal(healing, EHealLevel::OVERHEAL, power);
		EXPECT_EQ(healing, static_cast<int64_t>(UNIT_HEALTH) * count);
	}
};
}

TEST_F(CasualtyProvenanceTest, TemporaryOverhealConsumesCreatedSurplusBeforeRestoredCorpses)
{
	health.init();
	damageUnits(5, DamageProvenance::SPELL);
	ASSERT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 5);

	overHealUnits(10, EHealPower::ONE_BATTLE);
	ASSERT_EQ(health.getCount(), 15);
	ASSERT_EQ(health.getResurrected(), 10);
	ASSERT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 5);

	// The first five losses are Overheal-created excess units, not the five
	// original spell casualties that were temporarily raised.
	damageUnits(5, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getCount(), 10);
	EXPECT_EQ(health.getResurrected(), 5);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 5);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 0);

	// Only the following losses re-kill the restored corpse identities.
	damageUnits(5, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getCount(), 5);
	EXPECT_EQ(health.getResurrected(), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 5);

	health.init();
	damageUnits(5, DamageProvenance::SPELL);
	overHealUnits(10, EHealPower::ONE_BATTLE);
	health.takeResurrected();
	EXPECT_EQ(health.getCount(), 5);
	EXPECT_EQ(health.getResurrected(), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 5);
}

TEST_F(CasualtyProvenanceTest, PermanentOverhealRemovesOnlyRestoredDebtAndSurplusDamageIsNotACasualty)
{
	health.init();
	damageUnits(5, DamageProvenance::SPELL);
	ASSERT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 5);

	overHealUnits(10, EHealPower::PERMANENT);
	ASSERT_EQ(health.getCount(), 15);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);

	damageUnits(5, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getCount(), 10);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 0);

	damageUnits(1, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getCount(), 9);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 1);
}

TEST_F(CasualtyProvenanceTest, MixedCasualtiesRestoreNewestFirstAndTemporaryExpiryKeepsItsCause)
{
	health.init();
	damageUnits(3, DamageProvenance::PHYSICAL_CREATURE);
	damageUnits(2, DamageProvenance::SPELL);
	ASSERT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 3);
	ASSERT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 2);

	int64_t temporaryHealing = UNIT_HEALTH;
	health.heal(temporaryHealing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(temporaryHealing, UNIT_HEALTH);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 2);

	int64_t permanentHealing = UNIT_HEALTH;
	health.heal(permanentHealing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_EQ(permanentHealing, UNIT_HEALTH);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 1);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 3);

	health.takeResurrected();
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 1);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 3);
}

TEST_F(CasualtyProvenanceTest, ReDeathReplacesTemporaryCauseAndDestroyedRemainsCannotBeRaised)
{
	health.init();
	damageUnits(1, DamageProvenance::SPELL);
	int64_t temporaryHealing = UNIT_HEALTH;
	health.heal(temporaryHealing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(temporaryHealing, UNIT_HEALTH);

	damageUnits(1, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 1);

	health.init();
	damageUnits(1, DamageProvenance::SPELL);
	temporaryHealing = UNIT_HEALTH;
	health.heal(temporaryHealing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(temporaryHealing, UNIT_HEALTH);
	int64_t destroyingDamage = UNIT_HEALTH;
	health.damage(destroyingDamage, true, false, DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(health.getUnusableRemains(), 1);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::SPELL), 0);
	EXPECT_EQ(health.getCasualtyCount(DamageProvenance::PHYSICAL_CREATURE), 0);

	int64_t forbiddenResurrection = UNIT_HEALTH;
	health.heal(forbiddenResurrection, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(forbiddenResurrection, 0);
	EXPECT_EQ(health.getCount(), UNIT_AMOUNT - 1);
}
