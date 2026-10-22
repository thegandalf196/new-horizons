/*
 * FrozenPredicateCharacterizationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../AI/BattleAI/InitialExportWorld.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <array>
#include <stdexcept>

namespace
{
// Independent characterization of the pre-prefilter predicate. Deliberately
// does not call isFrozen, markerApplicationRound or packet validation.
bool originalFrozenPredicate(const battle::Unit & unit)
{
	return unit.hasBonus(CSelector([](const Bonus * bonus)
	{
		if(!bonus || bonus->type != BonusType::PHYSICAL_AFFLICTION
			|| bonus->source != BonusSource::CREATURE_ABILITY)
			return false;
		const auto metadata = physicalAfflictions::markerMetadata(*bonus);
		return metadata && metadata->kind == "frozen";
	}));
}

struct QueryResult
{
	bool value = false;
	std::string invalidArgument;
	bool operator==(const QueryResult &) const = default;
};

QueryResult query(const battle::Unit & unit, bool original)
{
	try
	{
		return {original ? originalFrozenPredicate(unit) : newHorizonsFrozen::isFrozen(unit), {}};
	}
	catch(const std::invalid_argument & error)
	{
		return {false, error.what()};
	}
}

void expectFrozen(const battle::Unit & unit, bool expected)
{
	const auto state = unit.acquireState()->save();
	EXPECT_EQ(query(unit, true), (QueryResult{expected, {}}));
	EXPECT_EQ(query(unit, false), (QueryResult{expected, {}}));
	EXPECT_EQ(query(unit, false), query(unit, true)); // Repeat warm queries.
	EXPECT_EQ(unit.acquireState()->save(), state);
}

class DetachedSubject
{
	::testing::NiceMock<UnitInfoMock> info;
	::testing::NiceMock<UnitEnvironmentMock> environment;
public:
	CBonusSystemNode bonuses{BonusNodeType::STACK_BATTLE};
	std::unique_ptr<battle::CUnitStateDetached> unit;
	DetachedSubject()
	{
		using namespace ::testing;
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(3));
		ON_CALL(info, unitId()).WillByDefault(Return(17));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(0).toCreature()));
		ON_CALL(info, unitOwner()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(info, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
		ON_CALL(info, unitSlot()).WillByDefault(Return(SlotID(0)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
		unit = std::make_unique<battle::CUnitStateDetached>(&info, &bonuses);
		unit->localInit(&environment);
		unit->setPosition(BattleHex(40));
	}
};
}

class FrozenPredicateCharacterizationTest : public ProjectedTargetHitFixture
{
protected:
	using SavedState = std::array<std::vector<std::byte>, 5>;
	SavedState savedState()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}

	Bonus marker() const
	{
		return newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound());
	}

	void nativeChange(const std::vector<Bonus> & added, const std::vector<Bonus> & removed)
	{
		// Retire live-backed hypothetical consumers before intentionally changing
		// their native source. Each query-only interval starts after this packet.
		child.reset();
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		if(!added.empty())
			packet.toAdd.emplace_back(incoming->unitId(), added);
		if(!removed.empty())
			packet.toRemove.emplace_back(incoming->unitId(), removed);
		gameHandler->sendAndApply(packet);
	}

	void overlayLifecycle()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto native = savedState();
		ASSERT_FALSE(child->initialExportSubject());
		auto left = std::make_shared<HypotheticBattle>(environment.get(), child);
		auto right = std::make_shared<HypotheticBattle>(environment.get(), child);
		const auto id = incoming->unitId();
		for(const auto & branch : {child, left, right})
		{
			const auto unit = branch->getForUpdate(id);
			EXPECT_FALSE(unit->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
			expectFrozen(*unit, false);
		}
		const auto parentState = child->getForUpdate(id)->save();
		const auto siblingState = right->getForUpdate(id)->save();
		left->addUnitBonus(id, {marker()});
		expectFrozen(*left->getForUpdate(id), true);
		EXPECT_EQ(left->getForUpdate(id)->frozenLastAppliedRound(), battle()->getRound());
		expectFrozen(*right->getForUpdate(id), false);
		expectFrozen(*child->getForUpdate(id), false);
		const auto physical = physicalAfflictions::first(*left->getForUpdate(id));
		ASSERT_TRUE(physical);
		ASSERT_EQ(physical->kind, "frozen");
		left->removeUnitBonus(id, physicalAfflictions::removalPlan(*left->getForUpdate(id), *physical));
		expectFrozen(*left->getForUpdate(id), false);
		EXPECT_FALSE(left->getForUpdate(id)->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
		EXPECT_EQ(left->getForUpdate(id)->frozenLastAppliedRound(), battle()->getRound());
		EXPECT_EQ(child->getForUpdate(id)->save(), parentState);
		EXPECT_EQ(right->getForUpdate(id)->save(), siblingState);
		expectFrozen(*incoming, false);
		EXPECT_EQ(savedState(), native);
	}
};

TEST_F(FrozenPredicateCharacterizationTest, EmptyAndUnrelatedNativeBonusesRetainWarmNegative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto native = savedState();
	DetachedSubject subject;
	expectFrozen(*subject.unit, false);
	EXPECT_FALSE(subject.unit->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
	for(int index = 0; index < 48; ++index)
		subject.bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_SPEED, BonusSource::OTHER, index, BonusSourceID()));
	expectFrozen(*subject.unit, false);
	EXPECT_FALSE(subject.unit->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_EQ(savedState(), native);
}

TEST_F(FrozenPredicateCharacterizationTest, ZeroMarkerSourceAndKindUseOnlyOriginalPredicateRequirements)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto native = savedState();
	for(int variant = 0; variant < 4; ++variant)
	{
		SCOPED_TRACE(variant);
		DetachedSubject subject;
		auto value = marker();
		ASSERT_EQ(value.val, 0);
		if(variant == 1)
			value.source = BonusSource::OTHER;
		if(variant == 2)
		{
			auto parameters = value.parameters->toCustom<JsonNode>();
			parameters["kind"].String() = "bleeding";
			parameters.Struct().erase("applicationRound");
			value.parameters = std::make_shared<BonusParameters>(parameters);
		}
		if(variant == 3)
		{
			// Recognition is intentionally less strict than application admission.
			value.duration = BonusDuration::PERMANENT;
			value.statusTags.clear();
			value.statusIdentity.clear();
			auto parameters = value.parameters->toCustom<JsonNode>();
			parameters.Struct().erase("applicationRound");
			value.parameters = std::make_shared<BonusParameters>(parameters);
		}
		subject.bonuses.addNewBonus(std::make_shared<Bonus>(value));
		EXPECT_TRUE(subject.unit->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
		expectFrozen(*subject.unit, variant == 0 || variant == 3);
	}
	EXPECT_EQ(savedState(), native);
}

TEST_F(FrozenPredicateCharacterizationTest, MalformedMetadataRetainsExactExceptionAndWrongSourceShortCircuit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto native = savedState();
	for(int variant = 0; variant < 7; ++variant)
	{
		SCOPED_TRACE(variant);
		DetachedSubject subject;
		auto value = marker();
		if(variant == 0)
			value.val = 1;
		else if(variant == 1 || variant == 6)
			value.parameters.reset();
		else if(variant == 2)
			value.parameters = std::make_shared<BonusParameters>(int32_t(7));
		else
		{
			auto parameters = value.parameters->toCustom<JsonNode>();
			if(variant == 3)
				parameters["unexpected"].Bool() = true;
			if(variant == 4)
				parameters["kind"].String().clear();
			if(variant == 5)
				parameters["applicationRound"].Integer() = -1;
			value.parameters = std::make_shared<BonusParameters>(parameters);
		}
		if(variant == 6)
			value.source = BonusSource::OTHER;
		subject.bonuses.addNewBonus(std::make_shared<Bonus>(value));
		const auto state = subject.unit->save();
		const auto original = query(*subject.unit, true);
		EXPECT_EQ(original.invalidArgument.empty(), variant == 6);
		EXPECT_FALSE(original.value);
		EXPECT_EQ(query(*subject.unit, false), original);
		EXPECT_EQ(query(*subject.unit, false), original);
		EXPECT_EQ(subject.unit->save(), state);
	}
	EXPECT_EQ(savedState(), native);
}

TEST_F(FrozenPredicateCharacterizationTest, NativePacketApplyAndPhysicalCleanseInvalidateWarmNegative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	child.reset();
	const auto before = savedState();
	EXPECT_FALSE(incoming->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
	expectFrozen(*incoming, false);
	EXPECT_EQ(savedState(), before);
	nativeChange({marker()}, {});
	const auto applied = savedState();
	expectFrozen(*incoming, true);
	const auto receipt = incoming->frozenLastAppliedRound();
	const auto physical = physicalAfflictions::first(*incoming);
	ASSERT_TRUE(physical);
	ASSERT_EQ(physical->kind, "frozen");
	const auto plan = physicalAfflictions::removalPlan(*incoming, *physical);
	ASSERT_EQ(plan.size(), 1u);
	EXPECT_EQ(savedState(), applied);
	nativeChange({}, plan);
	const auto cleansed = savedState();
	expectFrozen(*incoming, false);
	EXPECT_FALSE(incoming->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_EQ(incoming->frozenLastAppliedRound(), receipt);
	EXPECT_EQ(savedState(), cleansed);
}

TEST_F(FrozenPredicateCharacterizationTest, LegacyRecipientOverlayBranchesDoNotShareWarmNegativeOrCleanse)
{
	ASSERT_NO_FATAL_FAILURE(overlayLifecycle());
}

TEST_F(FrozenPredicateCharacterizationTest, PreparedProjectedExportsDoNotShareWarmNegativeOrRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto native = savedState();
	InitialExportWorld parent(*battle());
	size_t index = 0;
	while(index < parent.size() && parent.sourceAt(index).unitId != incoming->unitId())
		++index;
	ASSERT_LT(index, parent.size());
	auto left = parent.fork();
	auto right = parent.fork();
	for(const auto * world : {&parent, left.get(), right.get()})
	{
		EXPECT_FALSE(world->unitAt(index).hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
		expectFrozen(world->unitAt(index), false);
	}
	const auto parentState = parent.snapshotAt(index);
	const auto siblingState = right->snapshotAt(index);
	// Prepared export admission is a query-graph operation, not the unsupported
	// owned-AI status application/refresh route or a native Frozen receipt.
	const auto handle = left->add(left->sourceAt(index), marker());
	expectFrozen(left->unitAt(index), true);
	expectFrozen(parent.unitAt(index), false);
	expectFrozen(right->unitAt(index), false);
	left->remove(handle);
	expectFrozen(left->unitAt(index), false);
	EXPECT_FALSE(left->unitAt(index).hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_EQ(parent.snapshotAt(index), parentState);
	EXPECT_EQ(right->snapshotAt(index), siblingState);
	expectFrozen(*incoming, false);
	EXPECT_EQ(savedState(), native);
}
