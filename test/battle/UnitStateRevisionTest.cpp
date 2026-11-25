/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"

#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/json/JsonNode.h"

#include <cstddef>
#include <new>
#include <type_traits>

namespace test
{
using namespace ::testing;

static_assert(!std::is_copy_constructible_v<battle::HealthEdit>);
static_assert(!std::is_move_constructible_v<battle::HealthEdit>);
static_assert(!std::is_copy_constructible_v<battle::StateRevisionScope>);
static_assert(std::is_same_v<decltype(std::declval<const battle::CUnitState &>().getHealth()), const battle::CHealth &>);

TEST(UnitStateRevisionPrimitiveTest, scopeAndHealthHandleBumpWhenTheyStartAndEnd)
{
	battle::UnitStateRevision revision;
	const uint64_t initial = revision.value();
	uint64_t inside = 0;
	{
		const battle::StateRevisionScope scope(revision);
		inside = revision.value();
		EXPECT_GT(inside, initial);
	}
	const uint64_t after = revision.value();
	EXPECT_GT(after, inside);

	battle::CHealth health(nullptr);
	// The handle lives to the end of the full expression; the comma reads the value while it is open.
	const uint64_t during = (battle::HealthEdit(health, revision)->getCount(), revision.value());
	EXPECT_GT(during, after);
	EXPECT_GT(revision.value(), during);
}

TEST(UnitStateRevisionPrimitiveTest, distinctObjectsNeverShareAValue)
{
	battle::UnitStateRevision first, second;
	EXPECT_NE(first.value(), second.value());
	first.bump();
	EXPECT_GT(first.value(), second.value());
}

TEST(UnitStateRevisionPrimitiveTest, aUnitRecreatedInTheSameStorageNeverRepeatsARevision)
{
	UnitInfoMock info;
	BonusBearerMock bonuses;
	alignas(battle::CUnitStateDetached) std::byte storage[sizeof(battle::CUnitStateDetached)];
	auto * first = new(storage) battle::CUnitStateDetached(&info, &bonuses);
	const uint64_t created = first->getStateRevision();
	first->setGhost(false);
	const uint64_t last = first->getStateRevision();
	first->~CUnitStateDetached();

	auto * second = new(storage) battle::CUnitStateDetached(&info, &bonuses);
	ASSERT_EQ(static_cast<void *>(second), static_cast<void *>(first));
	EXPECT_GT(second->getStateRevision(), last);
	EXPECT_NE(second->getStateRevision(), created);
	second->~CUnitStateDetached();
}

class UnitStateRevisionTest : public Test
{
protected:
	static void SetUpTestSuite() { VCMI_TEST_REQUIRE_ORIGINAL_DATA(); }
	UnitInfoMock info, otherInfo;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses, otherBonuses;
	battle::CUnitStateDetached unit{&info, &bonuses};
	battle::CUnitStateDetached other{&otherInfo, &otherBonuses};

	void SetUp() override
	{
		for(auto * unitInfo : {&info, &otherInfo})
		{
			EXPECT_CALL(*unitInfo, unitBaseAmount()).WillRepeatedly(Return(10));
			EXPECT_CALL(*unitInfo, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		}
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(1));
		EXPECT_CALL(otherInfo, unitId()).WillRepeatedly(Return(2));
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		for(auto * bearer : {&bonuses, &otherBonuses})
		{
			bearer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 20, BonusSourceID()));
		}
		unit.localInit(&environment);
		other.localInit(&environment);
		unit.setPosition(BattleHex(5, 5));
		other.setPosition(BattleHex(10, 5));
	}

	template<typename Change> void expectBump(battle::CUnitState & changed, const char * name, Change && change)
	{
		const uint64_t before = changed.getStateRevision();
		change();
		EXPECT_GT(changed.getStateRevision(), before) << name;
	}
};

TEST_F(UnitStateRevisionTest, everyPlainStateMutatorBumpsTheRevision)
{
	expectBump(unit, "setPosition", [&] { unit.setPosition(BattleHex(6, 5)); });
	expectBump(unit, "setGhostPending", [&] { unit.setGhostPending(false); });
	expectBump(unit, "setGhost", [&] { unit.setGhost(false); });
	expectBump(unit, "setHealth", [&] { unit.setHealth(other.getHealth()); });
	expectBump(unit, "editHealth", [&] { unit.editHealth()->addTemporaryHitPoints(5); });
	expectBump(unit, "damage", [&] { int64_t amount = 30; unit.damage(amount); });
	expectBump(unit, "heal", [&] { int64_t amount = 10; unit.heal(amount, EHealLevel::HEAL, EHealPower::PERMANENT); });
	expectBump(unit, "shadow gift loss", [&] { unit.addShadowGiftMaximumHealthLoss(3); });
	expectBump(unit, "capacity preservation", [&] { unit.preserveCreatureHealthOnCapacityIncrease(); });
	expectBump(unit, "capacity normalization", [&] { unit.normalizeCapacityHealth(); });
	expectBump(unit, "capacity reference", [&] { unit.clearCapacityHealthReference(); });
	expectBump(unit, "beginBattleForm", [&] { unit.beginBattleForm(CreatureID(1), 2); });
	expectBump(unit, "form round", [&] { unit.afterNewRound(); });
	expectBump(unit, "deferBattleFormRestoration", [&] { unit.deferBattleFormRestoration(); });
	expectBump(unit, "endBattleForm", [&] { unit.endBattleForm(); });
	expectBump(unit, "makeGhost", [&] { unit.makeGhost(); });
	expectBump(unit, "onRemoved", [&] { unit.onRemoved(); });
	expectBump(unit, "localInit", [&] { unit.localInit(&environment); });
	const JsonNode saved = other.save();
	expectBump(unit, "load", [&] { unit.load(saved); });
}

TEST_F(UnitStateRevisionTest, phantomIntegrityChangesBumpTheRevision)
{
	unit.summoned = true;
	expectBump(unit, "initializePhantomProfile", [&] { unit.initializePhantomProfile(100, 2); });
	expectBump(unit, "phantom damage", [&] { int64_t amount = 40; unit.damage(amount); });
	EXPECT_EQ(unit.getPhantomIntegrity(), 60);
}

TEST_F(UnitStateRevisionTest, transfersBumpEveryChangedUnitAndNeverCopyTheRevision)
{
	other.setPosition(BattleHex(10, 5));
	const uint64_t source = other.getStateRevision();
	expectBump(unit, "assignment", [&] { unit = other; });
	EXPECT_EQ(other.getStateRevision(), source);
	EXPECT_NE(unit.getStateRevision(), source); // Its own history, not the source's value.
	EXPECT_EQ(unit.getPosition(), other.getPosition());

	const uint64_t unitBefore = unit.getStateRevision();
	const uint64_t otherBefore = other.getStateRevision();
	battle::exchangePreparedUnitStateValues(unit, other);
	EXPECT_GT(unit.getStateRevision(), unitBefore);
	EXPECT_GT(other.getStateRevision(), otherBefore);

	const auto snapshot = battle::UnitStateValueSnapshot::capture(unit);
	expectBump(unit, "snapshot restore", [&] { snapshot.restore(unit); });

	const uint64_t preparedBefore = other.getStateRevision();
	expectBump(unit, "commitPreparedCapacityHealth", [&] { unit.commitPreparedCapacityHealth(other); });
	EXPECT_GT(other.getStateRevision(), preparedBefore);
}

TEST_F(UnitStateRevisionTest, readsAndSavesLeaveTheRevisionAndSaveFormatUnchanged)
{
	int64_t amount = 25;
	unit.damage(amount);
	const uint64_t before = unit.getStateRevision();
	const JsonNode saved = unit.save();
	EXPECT_EQ(unit.getPosition(), BattleHex(5, 5));
	EXPECT_EQ(unit.getHealth().getCount(), 9);
	EXPECT_FALSE(unit.isGhost());
	EXPECT_FALSE(unit.isGhostPending());
	EXPECT_EQ(unit.getStateRevision(), before);

	EXPECT_FALSE(saved["state"].Struct().count("stateRevision"));
	other.load(saved);
	EXPECT_EQ(other.save().toCompactString(), saved.toCompactString());
	EXPECT_EQ(other.getHealth().getCount(), 9);
	EXPECT_EQ(other.getHealth().getFirstHPleft(), unit.getHealth().getFirstHPleft());
	EXPECT_EQ(other.getPosition(), BattleHex(5, 5));
}
}
