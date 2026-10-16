/*
 * UnitStateValueExchangeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/json/JsonNode.h"

#include <utility>
#include <stdexcept>

namespace test
{
using namespace ::testing;

class UnitStateValueExchangeTest : public Test
{
protected:
	UnitInfoMock firstInfo, secondInfo;
	UnitEnvironmentMock firstEnvironment, secondEnvironment;
	BonusBearerMock firstBonuses, secondBonuses;
	battle::CUnitStateDetached first{&firstInfo, &firstBonuses};
	battle::CUnitStateDetached second{&secondInfo, &secondBonuses};

	void add(BonusBearerMock & bearer, BonusType type, int value)
	{
		bearer.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			type, BonusSource::CREATURE_ABILITY, value, BonusSourceID()));
	}

	void SetUp() override
	{
		EXPECT_CALL(firstInfo, unitBaseAmount()).WillRepeatedly(Return(10));
		EXPECT_CALL(secondInfo, unitBaseAmount()).WillRepeatedly(Return(10));
		EXPECT_CALL(firstInfo, unitId()).WillRepeatedly(Return(1));
		EXPECT_CALL(secondInfo, unitId()).WillRepeatedly(Return(2));
		EXPECT_CALL(firstInfo, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(secondInfo, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(firstEnvironment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		EXPECT_CALL(secondEnvironment, unitHasAmmoCart(_)).WillRepeatedly(Return(true));
		for(auto * bearer : {&firstBonuses, &secondBonuses})
		{
			add(*bearer, BonusType::STACK_HEALTH, 20);
			add(*bearer, BonusType::SHOOTER, 1);
			add(*bearer, BonusType::CASTS, 3);
		}
		add(firstBonuses, BonusType::SHOTS, 5);
		add(secondBonuses, BonusType::SHOTS, 9);
		first.localInit(&firstEnvironment);
		second.localInit(&secondEnvironment);
		first.position = BattleHex(5, 5);
		second.position = BattleHex(10, 5);
	}

	void seedFirst()
	{
		first.defending = first.castSpellThisTurn = true;
		first.setActivationMovementBonus(3);
		first.veteranCohesionEarned = true;
		first.veteranPhysicalDamageSinceActivation = 9;
		first.archeryCrossfireRound = 3;
		first.archeryCrossfireAttackers = {11, 13};
		first.archeryCrossfireDefenders = {17};
		first.confusionState.previousResolved = battle::ConfusionBehavior::WANDER;
		first.confusionState.applyPending(PlayerColor(0), true);
		int64_t damage = 25;
		first.damage(damage, true);
		first.health.addTemporaryHitPoints(3);
		first.shots.use();
		first.casts.use();
		first.counterAttacks.use();
	}
};

TEST_F(UnitStateValueExchangeTest, CompleteSerializedValuesExchangeAndInverse)
{
	seedFirst();
	second.health.addTemporaryHitPoints(8);
	const auto beforeFirst = first.save(), beforeSecond = second.save();
	static_assert(noexcept(battle::exchangePreparedUnitStateValues(first, second)));
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.save(), beforeSecond);
	EXPECT_EQ(second.save(), beforeFirst);
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.save(), beforeFirst);
	EXPECT_EQ(second.save(), beforeSecond);
}

TEST_F(UnitStateValueExchangeTest, DescriptorAmmoCacheEnvironmentAndHealthOwnersStayBound)
{
	seedFirst();
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.unitId(), 1u);
	EXPECT_EQ(second.unitId(), 2u);
	EXPECT_EQ(first.shots.available(), 5);
	EXPECT_EQ(second.shots.available(), 8);
	first.shots.use();
	second.shots.use();
	EXPECT_EQ(first.shots.available(), 4);
	EXPECT_EQ(second.shots.available(), 8);
	EXPECT_CALL(secondInfo, unitBaseAmount()).WillRepeatedly(Return(12));
	first.health.init();
	second.health.init();
	EXPECT_EQ(first.getCount(), 10);
	EXPECT_EQ(second.getCount(), 12);
}

TEST_F(UnitStateValueExchangeTest, CopyUsesSameValuesAndPreservesNonFormOwners)
{
	seedFirst();
	const auto source = first.save();
	const auto revision = second.getBattleFormViewRevision();
	static_cast<battle::CUnitState &>(second) = static_cast<const battle::CUnitState &>(first);
	EXPECT_EQ(second.save(), source);
	EXPECT_EQ(first.save(), source);
	EXPECT_EQ(second.unitId(), 2u);
	EXPECT_EQ(second.shots.available(), 8);
	second.shots.use();
	EXPECT_EQ(second.shots.available(), 8);
	EXPECT_EQ(second.getBattleFormViewRevision(), revision);
}

TEST_F(UnitStateValueExchangeTest, DetachedFormValuesExchangeWithoutPretendingGraphCommit)
{
	first.beginBattleForm(CreatureID(1), 3);
	second.beginBattleForm(CreatureID(1), 4);
	first.deferBattleFormRestoration();
	first.defending = true;
	ASSERT_EQ(first.getMaxHealth(), second.getMaxHealth());
	ASSERT_EQ(first.battleFormCreature(), second.battleFormCreature());
	ASSERT_EQ(first.getBattleFormRoundsRemaining(), 1);
	ASSERT_TRUE(first.isBattleFormRestorationPending());
	const auto beforeFirst = first.save(), beforeSecond = second.save();
	const auto firstRevision = first.getBattleFormViewRevision();
	const auto secondRevision = second.getBattleFormViewRevision();
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.save(), beforeSecond);
	EXPECT_EQ(second.save(), beforeFirst);
	EXPECT_EQ(first.getBattleFormViewRevision(), firstRevision);
	EXPECT_EQ(second.getBattleFormViewRevision(), secondRevision);
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.save(), beforeFirst);
	EXPECT_EQ(second.save(), beforeSecond);
}

TEST_F(UnitStateValueExchangeTest, DifferentFormExchangeLeavesWarmCachesUntilGraphOwnerCommits)
{
	first.beginBattleForm(CreatureID(1), 3);
	first.deferBattleFormRestoration();
	const auto beforeFirst = first.save(), beforeSecond = second.save();
	const auto firstMaximum = first.getMaxHealth(), secondMaximum = second.getMaxHealth();
	ASSERT_NE(firstMaximum, secondMaximum);
	const auto firstVersion = first.getTreeVersion(), secondVersion = second.getTreeVersion();
	const auto firstRevision = first.getBattleFormViewRevision();
	const auto secondRevision = second.getBattleFormViewRevision();
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_FALSE(first.hasBattleForm());
	EXPECT_TRUE(second.hasBattleForm());
	EXPECT_EQ(second.getBattleFormRoundsRemaining(), 1);
	EXPECT_TRUE(second.isBattleFormRestorationPending());
	EXPECT_EQ(first.getTreeVersion(), firstVersion);
	EXPECT_EQ(second.getTreeVersion(), secondVersion);
	EXPECT_EQ(first.getBattleFormViewRevision(), firstRevision);
	EXPECT_EQ(second.getBattleFormViewRevision(), secondRevision);
	EXPECT_EQ(first.getMaxHealth(), firstMaximum);
	EXPECT_EQ(second.getMaxHealth(), secondMaximum);
	// This API is the value phase, not a completed form/graph transaction.
	try
	{
		second.save();
		FAIL() << "A different-form value phase must not masquerade as a completed graph commit";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Invalid active battle-form health state");
	}
	battle::exchangePreparedUnitStateValues(first, second);
	EXPECT_EQ(first.save(), beforeFirst);
	EXPECT_EQ(second.save(), beforeSecond);
}

TEST_F(UnitStateValueExchangeTest, CopyStillInvokesLegacyChangedFormHook)
{
	first.beginBattleForm(CreatureID(1), 3);
	const auto source = first.save();
	const auto revision = second.getBattleFormViewRevision();
	static_cast<battle::CUnitState &>(second) = static_cast<const battle::CUnitState &>(first);
	EXPECT_EQ(second.save(), source);
	EXPECT_EQ(first.save(), source);
	EXPECT_EQ(second.getBattleFormViewRevision(), revision + 1);
	second.endBattleForm();
	first.endBattleForm();
	EXPECT_EQ(second.save(), first.save());
}
}
