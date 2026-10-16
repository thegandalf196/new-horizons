/*
 * PreparedBonusGraphStorageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/Propagators.h"

#include <type_traits>

namespace test
{
using namespace ::testing;

class PreparedBonusGraphStorageTest : public Test
{
protected:
	CBonusSystemNode root{BonusNodeType::BATTLE_WIDE};
	CBonusSystemNode child{BonusNodeType::ARMY};
	CBonusSystemNode other{BonusNodeType::PLAYER};

	std::shared_ptr<Bonus> speed(int value)
	{
		return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
			BonusSource::CREATURE_ABILITY, value, BonusSourceID());
	}

	void SetUp() override
	{
		child.attachTo(root);
		other.attachTo(root);
		root.addNewBonus(speed(2));
		child.addNewBonus(speed(3));
		other.addNewBonus(speed(5));
	}

	static std::vector<std::shared_ptr<Bonus>> pointers(const BonusList & values)
	{
		return {values.begin(), values.end()};
	}

	void replaceSpeed(PreparedBonusGraphStorage & plan, size_t index, int value)
	{
		auto replacement = plan.projected(index);
		replacement.accepted.clear();
		replacement.exported.clear();
		auto bonus = speed(value);
		replacement.accepted.push_back(bonus);
		replacement.exported.push_back(bonus);
		plan.replace(index, std::move(replacement));
	}
};

TEST_F(PreparedBonusGraphStorageTest, ExactListsCommitWithoutMutatingRetainedQueries)
{
	static_assert(!std::is_copy_constructible_v<PreparedBonusGraphStorage>);
	static_assert(!std::is_move_constructible_v<PreparedBonusGraphStorage>);
	const auto retained = child.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "prepared-speed");
	const auto retainedPointers = pointers(*retained);
	const auto before = pointers(child.getExportedBonusList());
	PreparedBonusGraphStorage plan;
	const auto index = plan.capture(child);
	replaceSpeed(plan, index, 9);
	const auto replacement = pointers(plan.projected(index).exported);
	plan.addInvalidation(true, {&child});
	plan.prepare();
	EXPECT_EQ(pointers(child.getExportedBonusList()), before);
	EXPECT_EQ(retained->totalValue(), 5);
	ASSERT_TRUE(plan.readyToCommit());
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(pointers(child.getExportedBonusList()), replacement);
	EXPECT_EQ(child.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "prepared-speed")->totalValue(), 11);
	EXPECT_EQ(pointers(*retained), retainedPointers);
	EXPECT_EQ(retained->totalValue(), 5);
	EXPECT_FALSE(plan.commit());
}

TEST_F(PreparedBonusGraphStorageTest, LaterInvalidValueRefusesBeforeEitherNodeChanges)
{
	const auto firstBefore = pointers(child.getExportedBonusList());
	const auto secondBefore = pointers(other.getExportedBonusList());
	const auto firstVersion = child.getTreeVersion(), secondVersion = other.getTreeVersion();
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(child), 9);
	const auto second = plan.capture(other);
	auto invalid = plan.projected(second);
	invalid.accepted.push_back(nullptr);
	plan.replace(second, std::move(invalid));
	plan.addInvalidation(true, {&child, &other});
	EXPECT_THROW(plan.prepare(), std::invalid_argument);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(pointers(child.getExportedBonusList()), firstBefore);
	EXPECT_EQ(pointers(other.getExportedBonusList()), secondBefore);
	EXPECT_EQ(child.getTreeVersion(), firstVersion);
	EXPECT_EQ(other.getTreeVersion(), secondVersion);
}

TEST_F(PreparedBonusGraphStorageTest, StaleLaterNodeRefusesWholePreparedCommit)
{
	const auto before = pointers(child.getExportedBonusList());
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(child), 9);
	plan.capture(other);
	plan.addInvalidation(true, {&child});
	plan.prepare();
	other.nodeHasChanged();
	EXPECT_FALSE(plan.stillCurrent());
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(pointers(child.getExportedBonusList()), before);
}

TEST_F(PreparedBonusGraphStorageTest, ExactEdgeStorageCanReparentWithoutReplacingNodes)
{
	PreparedBonusGraphStorage plan;
	const auto rootIndex = plan.capture(root), childIndex = plan.capture(child), otherIndex = plan.capture(other);
	auto rootValues = plan.projected(rootIndex);
	rootValues.children.erase(std::find(rootValues.children.begin(), rootValues.children.end(), &other));
	auto childValues = plan.projected(childIndex);
	childValues.children.push_back(&other);
	auto otherValues = plan.projected(otherIndex);
	otherValues.parents = {&child};
	otherValues.propagatingParents = {&child};
	plan.replace(rootIndex, std::move(rootValues));
	plan.replace(childIndex, std::move(childValues));
	plan.replace(otherIndex, std::move(otherValues));
	plan.addInvalidation(true, {&other});
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(other.getParentNodes().size(), 1u);
	EXPECT_EQ(other.getParentNodes().front(), &child);
	EXPECT_EQ(other.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED))->totalValue(), 10);
}

TEST_F(PreparedBonusGraphStorageTest, SourceOnlyParentNeedsNoInventedReciprocalChild)
{
	CBonusSystemNode source{BonusNodeType::CREATURE};
	PreparedBonusGraphStorage plan;
	const auto index = plan.capture(child);
	auto values = plan.projected(index);
	values.parents.push_back(&source);
	plan.replace(index, std::move(values));
	plan.addInvalidation(true, {&child});
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(child.getParentNodes().back(), &source);
	// Source-only attachment is not an attachTo child/propagation-parent pair.
	child.detachFromSource(source);
}

TEST_F(PreparedBonusGraphStorageTest, RepeatedCaptureAndReplacementKeepLatestPrivateProjection)
{
	const auto before = pointers(child.getExportedBonusList());
	PreparedBonusGraphStorage plan;
	const auto index = plan.capture(child);
	replaceSpeed(plan, index, 6);
	EXPECT_EQ(plan.capture(child), index);
	EXPECT_EQ(plan.projected(index).exported.front()->val, 6);
	replaceSpeed(plan, index, 9);
	EXPECT_EQ(pointers(child.getExportedBonusList()), before);
	plan.addInvalidation(true, {&child});
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(child.getExportedBonusList().front()->val, 9);
}

TEST_F(PreparedBonusGraphStorageTest, PreparedOwnershipRetainsRemovedBonusUntilPlanDestruction)
{
	std::weak_ptr<Bonus> removed = child.getExportedBonusList().front();
	{
		PreparedBonusGraphStorage plan;
		const auto index = plan.capture(child);
		auto empty = plan.projected(index);
		empty.accepted.clear();
		empty.exported.clear();
		plan.replace(index, std::move(empty));
		plan.addInvalidation(true, {&child});
		plan.prepare();
		ASSERT_TRUE(plan.commit());
		EXPECT_TRUE(child.getExportedBonusList().empty());
		EXPECT_FALSE(removed.expired());
	}
	EXPECT_TRUE(removed.expired());
}

TEST_F(PreparedBonusGraphStorageTest, CurrentGenerationAndNextGenerationEventsRemainDistinct)
{
	PreparedBonusGraphStorage plan;
	plan.capture(child);
	plan.capture(other);
	plan.addInvalidation(true, {&child});
	plan.addInvalidation(false, {&other});
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(child.getTreeVersion(), other.getTreeVersion());
	PreparedBonusGraphStorage next;
	next.capture(child);
	next.addInvalidation(true, {&child});
	next.prepare();
	ASSERT_TRUE(next.commit());
	EXPECT_GT(child.getTreeVersion(), other.getTreeVersion());
}

TEST_F(PreparedBonusGraphStorageTest, InvalidProtocolInputsDoNotBecomeGameplayModeRestrictions)
{
	PreparedBonusGraphStorage plan;
	const auto index = plan.capture(child);
	EXPECT_FALSE(plan.commit());
	EXPECT_THROW(plan.addInvalidation(true, {&other}), std::invalid_argument);
	EXPECT_THROW(plan.addInvalidation(true, {&child, &child}), std::invalid_argument);
	replaceSpeed(plan, index, 9);
	EXPECT_THROW(plan.prepare(), std::invalid_argument);
	plan.addInvalidation(true, {&child});
	plan.prepare();
	EXPECT_THROW(plan.capture(other), std::logic_error);
	EXPECT_THROW(plan.replace(index, plan.projected(index)), std::logic_error);
	ASSERT_TRUE(plan.commit());
	EXPECT_THROW(plan.projected(index), std::logic_error);
	EXPECT_FALSE(plan.commit());
}

TEST_F(PreparedBonusGraphStorageTest, PrecomputedPropagatedRemovalRetainsExactSurvivingPointers)
{
	const auto survivor = child.getExportedBonusList().front();
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::ARMY);
	child.addNewBonus(aura);
	EXPECT_THROW(child.prepareLocalBonusReplacement({survivor}), std::runtime_error);
	PreparedBonusGraphStorage plan;
	const auto index = plan.capture(child);
	auto values = plan.projected(index);
	values.accepted -= aura;
	values.exported -= aura;
	plan.replace(index, std::move(values));
	plan.addInvalidation(true, {&child});
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(child.getExportedBonusList().size(), 1u);
	EXPECT_EQ(child.getExportedBonusList().front(), survivor);
	EXPECT_EQ(child.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED))->totalValue(), 5);
	EXPECT_EQ(aura->val, 7);
}

TEST_F(PreparedBonusGraphStorageTest, WarmFalseOnlyProgramRefusesWithoutPoisoningNamedCache)
{
	CBonusSystemNode isolated{BonusNodeType::ARMY};
	isolated.addNewBonus(speed(2));
	const auto warm = isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "warm-false");
	const auto version = isolated.getTreeVersion();
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(isolated), 9);
	plan.addInvalidation(false, {&isolated});
	EXPECT_THROW(plan.prepare(), std::invalid_argument);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(isolated.getTreeVersion(), version);
	EXPECT_EQ(isolated.getExportedBonusList().front()->val, 2);
	EXPECT_EQ(isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "warm-false"), warm);
	EXPECT_EQ(warm->totalValue(), 2);
}

TEST_F(PreparedBonusGraphStorageTest, BorrowedFalseInvalidatesOlderWarmCacheWithoutAdvancing)
{
	CBonusSystemNode isolated{BonusNodeType::ARMY}, unrelated{BonusNodeType::PLAYER};
	isolated.addNewBonus(speed(2));
	const auto warm = isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "borrowed-false");
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(isolated), 9);
	plan.addInvalidation(false, {&isolated});
	unrelated.nodeHasChanged();
	const auto borrowed = unrelated.getTreeVersion();
	ASSERT_NO_THROW(plan.prepare());
	ASSERT_TRUE(plan.readyToCommit());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(isolated.getTreeVersion(), borrowed);
	EXPECT_EQ(isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "borrowed-false")->totalValue(), 9);
	EXPECT_EQ(warm->totalValue(), 2);
}

TEST_F(PreparedBonusGraphStorageTest, BorrowedGenerationAdvancedAfterPrepareRefusesWithoutWrites)
{
	CBonusSystemNode isolated{BonusNodeType::ARMY}, unrelated{BonusNodeType::PLAYER};
	isolated.addNewBonus(speed(2));
	const auto warm = isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "stale-borrow");
	const auto originalVersion = isolated.getTreeVersion();
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(isolated), 9);
	plan.addInvalidation(false, {&isolated});
	unrelated.nodeHasChanged();
	plan.prepare();
	unrelated.nodeHasChanged();
	EXPECT_FALSE(plan.stillCurrent());
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(isolated.getTreeVersion(), originalVersion);
	EXPECT_EQ(isolated.getExportedBonusList().front()->val, 2);
	EXPECT_EQ(isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "stale-borrow"), warm);
}

TEST_F(PreparedBonusGraphStorageTest, TrueThenFalseRevalidatesAfterUnrelatedGenerationAdvance)
{
	child.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "rebase-child");
	other.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "rebase-other");
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(child), 9);
	replaceSpeed(plan, plan.capture(other), 8);
	plan.addInvalidation(true, {&child});
	plan.addInvalidation(false, {&other});
	plan.prepare();
	CBonusSystemNode unrelated{BonusNodeType::PLAYER};
	unrelated.nodeHasChanged();
	ASSERT_TRUE(plan.stillCurrent());
	ASSERT_TRUE(plan.readyToCommit());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(child.getTreeVersion(), other.getTreeVersion());
	EXPECT_EQ(child.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "rebase-child")->totalValue(), 11);
	EXPECT_EQ(other.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "rebase-other")->totalValue(), 10);
}

TEST_F(PreparedBonusGraphStorageTest, FalseOnlyLaterWarmNodeBlocksEarlierOtherwiseFreshNode)
{
	CBonusSystemNode firstNode{BonusNodeType::ARMY}, laterNode{BonusNodeType::ARMY};
	firstNode.addNewBonus(speed(2));
	const auto firstWarm = firstNode.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "first-warm");
	laterNode.addNewBonus(speed(4));
	const auto laterWarm = laterNode.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "later-warm");
	const auto firstVersion = firstNode.getTreeVersion(), laterVersion = laterNode.getTreeVersion();
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(firstNode), 9);
	replaceSpeed(plan, plan.capture(laterNode), 10);
	plan.addInvalidation(false, {&firstNode, &laterNode});
	EXPECT_THROW(plan.prepare(), std::invalid_argument);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(firstNode.getTreeVersion(), firstVersion);
	EXPECT_EQ(laterNode.getTreeVersion(), laterVersion);
	EXPECT_EQ(firstNode.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "first-warm"), firstWarm);
	EXPECT_EQ(laterNode.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "later-warm"), laterWarm);
	EXPECT_EQ(firstWarm->totalValue(), 2);
	EXPECT_EQ(laterWarm->totalValue(), 4);
}

TEST_F(PreparedBonusGraphStorageTest, CacheFilledAfterCaptureRefusesBeforePreparedWrites)
{
	CBonusSystemNode isolated{BonusNodeType::ARMY};
	isolated.addNewBonus(speed(2));
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(isolated), 9);
	plan.addInvalidation(true, {&isolated});
	const auto warm = isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "late-reader");
	EXPECT_THROW(plan.prepare(), std::runtime_error);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(isolated.getExportedBonusList().front()->val, 2);
	EXPECT_EQ(isolated.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "late-reader"), warm);
}

TEST_F(PreparedBonusGraphStorageTest, WarmDependentWithoutLocalChangesRejectsBorrowedStampCollision)
{
	CBonusSystemNode parent{BonusNodeType::ARMY}, dependent{BonusNodeType::ARMY};
	parent.addNewBonus(speed(2));
	dependent.attachTo(parent);
	dependent.addNewBonus(speed(3));
	const auto selector = Selector::type()(BonusType::STACKS_SPEED);
	const auto parentWarm = parent.getAllBonuses(selector, "parent-warm");
	const auto childWarm = dependent.getAllBonuses(selector, "dependent-warm");
	ASSERT_EQ(parentWarm->totalValue(), 2);
	ASSERT_EQ(childWarm->totalValue(), 5);
	const auto parentVersion = parent.getTreeVersion(), childVersion = dependent.getTreeVersion();
	const auto childStorage = pointers(dependent.getExportedBonusList());
	PreparedBonusGraphStorage plan;
	replaceSpeed(plan, plan.capture(parent), 9);
	plan.capture(dependent);
	plan.addInvalidation(false, {&parent, &dependent});
	EXPECT_THROW(plan.prepare(), std::invalid_argument);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(parent.getTreeVersion(), parentVersion);
	EXPECT_EQ(dependent.getTreeVersion(), childVersion);
	EXPECT_EQ(parent.getExportedBonusList().front()->val, 2);
	EXPECT_EQ(pointers(dependent.getExportedBonusList()), childStorage);
	EXPECT_EQ(dependent.getAllBonuses(selector, "dependent-warm"), childWarm);
	// The caller can explicitly request a fresh generation; refusal never promotes false.
	plan.addInvalidation(true, {&parent, &dependent});
	ASSERT_NO_THROW(plan.prepare());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(parent.getAllBonuses(selector, "parent-warm")->totalValue(), 9);
	EXPECT_EQ(dependent.getAllBonuses(selector, "dependent-warm")->totalValue(), 12);
	EXPECT_EQ(pointers(dependent.getExportedBonusList()), childStorage);
	EXPECT_EQ(parentWarm->totalValue(), 2);
	EXPECT_EQ(childWarm->totalValue(), 5);
}
}
