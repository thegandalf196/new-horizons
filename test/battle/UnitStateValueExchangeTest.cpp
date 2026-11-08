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
#include "../../lib/battle/StackInitializationReceipts.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/CStack.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
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
		first.arcaneMarks.set(BattleSide::ATTACKER, 3);
		first.arcaneMarks.set(BattleSide::DEFENDER, 1);
		first.demonicGateOpened = true;
		first.physicalPoisonBaseDamage = 10;
		first.physicalPoisonActivationsRemaining = 2;
		first.physicalPoisonSourceStackId = -1;
		first.physicalPoisonSpellSide = BattleSide::ATTACKER;
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
TEST_F(UnitStateValueExchangeTest, PassiveFactionReceiptsSurviveNativeRebindingAndStrictLoad)
{
	seedFirst();
	const auto marks = first.arcaneMarks;
	const battle::StackInitializationReceipts receipts(first);
	receipts.initialize(first, &firstEnvironment, first.position);
	EXPECT_EQ(first.arcaneMarks, marks);
	EXPECT_TRUE(first.hasOpenedDemonicGate());
	EXPECT_EQ(first.physicalPoisonSpellOrigin(), BattleSide::ATTACKER);
	EXPECT_EQ(first.physicalPoisonBaseDamage, 10);
	EXPECT_EQ(first.physicalPoisonActivationsRemaining, 2);
	const auto saved = first.save();
	second.load(saved);
	EXPECT_EQ(second.arcaneMarks, marks);
	EXPECT_TRUE(second.hasOpenedDemonicGate());
	auto invalid = saved;
	invalid["state"]["arcaneMarks"].Vector()[0].Integer() = 6;
	const auto before = second.save();
	EXPECT_THROW(second.load(invalid), std::runtime_error);
	EXPECT_EQ(second.save(), before);
}

TEST_F(UnitStateValueExchangeTest, PoisonReplacementAndPurifyClearOnlySuccessfulSpellProvenance)
{
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(&first, 10, -1));
	first.physicalPoisonSpellSide = BattleSide::DEFENDER;
	EXPECT_FALSE(newHorizonsBulwark::applyPhysicalPoison(&first, 5, 8));
	EXPECT_EQ(first.physicalPoisonSpellOrigin(), BattleSide::DEFENDER);
	EXPECT_TRUE(newHorizonsBulwark::applyPhysicalPoison(&first, 10, 8));
	EXPECT_EQ(first.physicalPoisonSpellOrigin(), BattleSide::NONE);
	first.physicalPoisonSpellSide = BattleSide::ATTACKER;
	EXPECT_TRUE(newHorizonsPurify::clearPhysicalPoison(&first));
	EXPECT_EQ(first.physicalPoisonSpellOrigin(), BattleSide::NONE);
	EXPECT_EQ(first.physicalPoisonBaseDamage, 0);
	EXPECT_EQ(first.physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(first.physicalPoisonSourceStackId, -1);
}
TEST_F(UnitStateValueExchangeTest, PoisonTupleHasCoherent1155DescriptorAndOlderPrefixRefusal)
{
	CStackBasicDescriptor base(CreatureID(0), 10);
	CStack descriptor(&base, PlayerColor(0), 1, BattleSide::ATTACKER);
	descriptor.physicalPoisonBaseDamage = 10;
	descriptor.physicalPoisonActivationsRemaining = 2;
	descriptor.physicalPoisonSourceStackId = -1;
	descriptor.physicalPoisonSpellSide = BattleSide::DEFENDER;
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
	bytes.iser.version = bytes.oser.version;
	bytes.oser & descriptor;
	CStack restored;
	bytes.iser & restored;
	EXPECT_EQ(restored.physicalPoisonBaseDamage, 10);
	EXPECT_EQ(restored.physicalPoisonActivationsRemaining, 2);
	EXPECT_EQ(restored.physicalPoisonSourceStackId, -1);
	EXPECT_EQ(restored.physicalPoisonSpellOrigin(), BattleSide::DEFENDER);
	for(const auto origin : {BattleSide::NONE, BattleSide::ATTACKER})
	{
		descriptor.physicalPoisonSpellSide = origin;
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
		EXPECT_THROW(old.oser & descriptor, std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	}
	descriptor.physicalPoisonBaseDamage = 0;
	descriptor.physicalPoisonActivationsRemaining = 0;
	descriptor.physicalPoisonSourceStackId = -1;
	descriptor.physicalPoisonSpellSide = BattleSide::NONE;
	CMemorySerializer emptyHistorical;
	emptyHistorical.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	emptyHistorical.iser.version = emptyHistorical.oser.version;
	emptyHistorical.oser & descriptor;
	emptyHistorical.iser & restored;
	EXPECT_EQ(restored.physicalPoisonBaseDamage, 0);
	EXPECT_EQ(restored.physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(restored.physicalPoisonSourceStackId, -1);
	EXPECT_EQ(restored.physicalPoisonSpellOrigin(), BattleSide::NONE);
}

TEST_F(UnitStateValueExchangeTest, UnsupportedPassiveUnitReadersRejectBeforeGameplayApplication)
{
	const auto before = second.save();
	for(const int receipt : {0, 1, 2})
	{
		UnitChanges incoming(2, UnitChanges::EOperation::UPDATE);
		incoming.data = before;
		if(receipt == 0) incoming.data["state"]["arcaneMarks"].Vector()[0].Integer() = 1;
		if(receipt == 1) incoming.data["state"]["demonicGateOpened"].Bool() = true;
		if(receipt == 2)
		{
			incoming.data["state"]["physicalPoisonBaseDamage"].Integer() = 10;
			incoming.data["state"]["physicalPoisonActivationsRemaining"].Integer() = 2;
			incoming.data["state"]["physicalPoisonSpellSide"].Integer() = 1;
		}
		CMemorySerializer direct;
		direct.oser.version = ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
		direct.iser.version = static_cast<ESerializationVersion>(1154);
		direct.oser & incoming;
		bool directReadCompleted = false;
		try
		{
			UnitChanges decoded;
			direct.iser & decoded;
			directReadCompleted = true;
			second.load(decoded.data);
		}
		catch(const std::runtime_error &) {}
		EXPECT_FALSE(directReadCompleted);
		EXPECT_EQ(second.save(), before);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.push_back(incoming);
		CMemorySerializer outer;
		outer.oser.version = ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
		outer.iser.version = static_cast<ESerializationVersion>(1154);
		outer.oser & packet;
		bool outerReadCompleted = false;
		try
		{
			BattleUnitsChanged decoded;
			outer.iser & decoded;
			outerReadCompleted = true;
			for(const auto & change : decoded.changedStacks) second.load(change.data);
		}
		catch(const std::runtime_error &) {}
		EXPECT_FALSE(outerReadCompleted);
		EXPECT_EQ(second.save(), before);
	}
}
TEST_F(UnitStateValueExchangeTest, HistoricalEmptyPassiveUnitReceiptRemainsReadable)
{
	UnitChanges incoming(2, UnitChanges::EOperation::UPDATE);
	incoming.data = second.save();
	CMemorySerializer bytes;
	bytes.oser.version = bytes.iser.version = static_cast<ESerializationVersion>(1154);
	ASSERT_NO_THROW(bytes.oser & incoming);
	UnitChanges decoded;
	ASSERT_NO_THROW(bytes.iser & decoded);
	EXPECT_EQ(decoded.data, incoming.data);
	EXPECT_NO_THROW(second.load(decoded.data));
	EXPECT_EQ(second.metamagicMarks(BattleSide::ATTACKER), 0);
	EXPECT_FALSE(second.hasOpenedDemonicGate());
	EXPECT_EQ(second.physicalPoisonSpellOrigin(), BattleSide::NONE);
}

TEST_F(UnitStateValueExchangeTest, AcquireCopiesCompleteStateBeforeTheEnvironmentCanObserveAClone)
{
	seedFirst();
	struct CompleteStateEnvironment final : UnitEnvironmentMock
	{
		const battle::CUnitState & source;
		mutable int queries = 0;
		explicit CompleteStateEnvironment(const battle::CUnitState & source) : source(source) {}
		newHorizonsElementalAttunement::UnitPassives unitElementalAttunement(const battle::Unit * unit) const override
		{
			++queries;
			const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
			if(!state || state->defending != source.defending
				|| state->castSpellThisTurn != source.castSpellThisTurn
				|| state->physicalPoisonBaseDamage != source.physicalPoisonBaseDamage
				|| state->getPosition() != source.getPosition())
				throw std::logic_error("Environment observed a reset, unpublished clone");
			return {};
		}
	} environment(first);
	first.bindStateEnvironment(&environment);
	auto restore = vstd::makeScopeGuard([&] { first.bindStateEnvironment(&firstEnvironment); });
	std::shared_ptr<battle::CUnitState> typed;
	std::shared_ptr<battle::Unit> generic;
	ASSERT_NO_THROW(typed = first.acquireState());
	ASSERT_NO_THROW(generic = first.acquire());
	EXPECT_EQ(environment.queries, 0);
	ASSERT_NE(typed, nullptr); ASSERT_NE(generic, nullptr);
	const auto source = first.save();
	EXPECT_EQ(typed->save(), source);
	EXPECT_EQ(std::dynamic_pointer_cast<battle::CUnitState>(generic)->save(), source);
	EXPECT_EQ(typed->getMaxHealth(), 20);
	EXPECT_TRUE(typed->usesStateEnvironment(environment));
	EXPECT_EQ(typed->shots.available(), first.shots.available());
	EXPECT_EQ(typed->counterAttacks.available(), first.counterAttacks.available());
	const auto before = first.getAvailableHealth();
	int64_t damage = 1;
	typed->damage(damage);
	typed->shots.use();
	EXPECT_EQ(first.getAvailableHealth(), before);
	EXPECT_EQ(first.save(), source);
}

}
