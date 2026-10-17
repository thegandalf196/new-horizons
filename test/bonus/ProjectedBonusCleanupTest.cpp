/*
 * ProjectedBonusCleanupTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ProjectedUnitEnvironmentFixture.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/bonuses/Propagators.h"
#include <new>
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> speed(int value, BonusDuration::Type duration = BonusDuration::PERMANENT)
{
	return std::make_shared<Bonus>(duration, BonusType::STACKS_SPEED, BonusSource::OTHER, value, BonusSourceID());
}

std::vector<std::shared_ptr<Bonus>> pointers(const BonusList & values)
{
	return {values.begin(), values.end()};
}
}

class ProjectedBonusCleanupTest : public Test
{
protected:
	CBonusSystemNode root{BonusNodeType::PLAYER}, left{BonusNodeType::ARMY};
	CBonusSystemNode right{BonusNodeType::ARMY}, leaf{BonusNodeType::STACK_BATTLE};
	ProjectedBonusGraph graph;
	TNodesVector authority{&root, &left, &right, &leaf};
	void SetUp() override
	{
		left.attachTo(root);
		right.attachTo(root);
		leaf.attachTo(left);
		leaf.attachTo(right);
	}
};

TEST_F(ProjectedBonusCleanupTest, ExportSnapshotThenDepthFirstChildOrderPreservesRepeatedDiamondPaths)
{
	root.addNewBonus(speed(1, BonusDuration::UNTIL_BEING_ATTACKED));
	left.addNewBonus(speed(2, BonusDuration::UNTIL_BEING_ATTACKED));
	right.addNewBonus(speed(3, BonusDuration::UNTIL_BEING_ATTACKED));
	leaf.addNewBonus(speed(4, BonusDuration::UNTIL_BEING_ATTACKED));
	leaf.addNewBonus(speed(5));
	graph.capture(root);
	std::vector<int> projectedCalls, liveCalls;
	graph.removeBonusesRecursive(root, CSelector([&](const Bonus * bonus)
	{
		projectedCalls.push_back(bonus->val);
		return Bonus::UntilBeingAttacked(bonus);
	}));
	EXPECT_EQ(projectedCalls, (std::vector<int>{1, 2, 4, 5, 3, 5}));
	EXPECT_EQ(root.getExportedBonusList().size(), 1);
	EXPECT_EQ(leaf.getExportedBonusList().size(), 2);
	root.removeBonusesRecursive(CSelector([&](const Bonus * bonus)
	{
		liveCalls.push_back(bonus->val);
		return Bonus::UntilBeingAttacked(bonus);
	}));
	EXPECT_EQ(projectedCalls, liveCalls);
}

TEST_F(ProjectedBonusCleanupTest, WarmCachedCommitMatchesLiveCleanupPointersQueriesAndGenerationCount)
{
	auto rootEffect = speed(2, BonusDuration::UNTIL_BEING_ATTACKED);
	auto leftEffect = speed(3, BonusDuration::UNTIL_BEING_ATTACKED);
	auto permanent = speed(5);
	root.addNewBonus(rootEffect);
	left.addNewBonus(leftEffect);
	leaf.addNewBonus(permanent);
	const auto retained = leaf.getAllBonuses(Selector::all, "cleanup");
	const auto retainedPointers = pointers(*retained);
	const auto original = pointers(root.getExportedBonusList());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.capture(root);
	graph.removeBonusesRecursive(root, Bonus::UntilBeingAttacked);
	graph.nodeHasChanged(root); // separate legacy negative-health event
	EXPECT_EQ(pointers(root.getExportedBonusList()), original);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(graph.query(leaf, Selector::all, false)->totalValue(), 5);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.readyToCommit());
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "cleanup")->totalValue(), 5);
	EXPECT_EQ(retained->totalValue(), 10);
	EXPECT_EQ(pointers(*retained), retainedPointers);
	EXPECT_EQ(leaf.getExportedBonusList().front(), permanent);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 3);
}

TEST_F(ProjectedBonusCleanupTest, UnmatchedCleanupEmitsNoExtraEventButExplicitSizeInvalidationStillDoes)
{
	auto permanent = speed(7);
	root.addNewBonus(permanent);
	graph.capture(root);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.removeBonusesRecursive(root, Bonus::UntilBeingAttacked);
	graph.nodeHasChanged(root);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 1);
	EXPECT_EQ(root.getExportedBonusList().front(), permanent);
}

TEST_F(ProjectedBonusCleanupTest, DistinctPointerOccurrencesAreRemovedWithoutDeduplicatingAuthoredExports)
{
	auto first = speed(2, BonusDuration::UNTIL_BEING_ATTACKED);
	auto second = speed(2, BonusDuration::UNTIL_BEING_ATTACKED);
	root.addNewBonus(first);
	root.addNewBonus(second);
	graph.capture(root);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.removeBonusesRecursive(root, Bonus::UntilBeingAttacked);
	ASSERT_TRUE(graph.values(root).exported.empty());
	EXPECT_EQ(root.getExportedBonusList().size(), 2);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 2);
	EXPECT_TRUE(root.getExportedBonusList().empty());
}

TEST_F(ProjectedBonusCleanupTest, PropagatedRemovalRetainsSharedUnmatchedPointersAndBorrowedGenerationPolicy)
{
	auto propagated = speed(4, BonusDuration::UNTIL_BEING_ATTACKED);
	propagated->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	root.addNewBonus(propagated);
	auto permanent = speed(6);
	leaf.addNewBonus(permanent);
	graph.capture(root);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.removeBonusesRecursive(root, Bonus::UntilBeingAttacked);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(graph.query(leaf, Selector::all, false)->totalValue(), 6);
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "propagated-cleanup")->totalValue(), 10);
	EXPECT_EQ(graph.values(leaf).accepted.front(), permanent);
	// No preceding advancing event: preserve possible borrowed-stamp refusal,
	// never promote this removal into a made-up global generation increment.
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, authority)), std::logic_error);
}

TEST_F(ProjectedBonusCleanupTest, SelectorAllocationRefusalAfterPrefixPoisonsOnlyPrivateOwner)
{
	root.addNewBonus(speed(2, BonusDuration::UNTIL_BEING_ATTACKED));
	left.addNewBonus(speed(3, BonusDuration::UNTIL_BEING_ATTACKED));
	const auto rootBefore = pointers(root.getExportedBonusList());
	const auto leftBefore = pointers(left.getExportedBonusList());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.capture(root);
	EXPECT_THROW(graph.removeBonusesRecursive(root, CSelector([](const Bonus * bonus)
	{
		if(bonus->val == 3)
			throw std::bad_alloc();
		return true;
	})), std::bad_alloc);
	EXPECT_EQ(pointers(root.getExportedBonusList()), rootBefore);
	EXPECT_EQ(pointers(left.getExportedBonusList()), leftBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.query(root, Selector::all, false), std::logic_error);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, authority)), std::logic_error);
}

TEST_F(ProjectedBonusCleanupTest, PriorAdvancingEventAllowsOrderedPropagatedCleanupWithoutInventedPromotion)
{
	auto propagated = speed(4, BonusDuration::UNTIL_BEING_ATTACKED);
	propagated->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	root.addNewBonus(propagated);
	leaf.addNewBonus(speed(6));
	graph.capture(root);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.nodeHasChanged(root);
	graph.removeBonusesRecursive(root, Bonus::UntilBeingAttacked);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 1);
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "advanced-cleanup")->totalValue(), 6);
}

TEST_F(ProjectedBonusCleanupTest, SelectionCompletesForOneNodeBeforeFirstRemoval)
{
	root.addNewBonus(speed(2, BonusDuration::UNTIL_BEING_ATTACKED));
	root.addNewBonus(speed(3, BonusDuration::UNTIL_BEING_ATTACKED));
	graph.capture(root);
	int calls = 0;
	graph.removeBonusesRecursive(root, CSelector([&](const Bonus *)
	{
		++calls;
		EXPECT_EQ(graph.values(root).exported.size(), 2);
		return true;
	}));
	EXPECT_EQ(calls, 2);
	EXPECT_TRUE(graph.values(root).exported.empty());
}

TEST_F(ProjectedBonusCleanupTest, LiveMutationAfterCaptureRefusesBeforeSelectorOrStagedRemoval)
{
	root.addNewBonus(speed(2, BonusDuration::UNTIL_BEING_ATTACKED));
	graph.capture(root);
	root.nodeHasChanged();
	int calls = 0;
	EXPECT_THROW(graph.removeBonusesRecursive(root, CSelector([&](const Bonus *)
	{
		++calls;
		return true;
	})), std::logic_error);
	EXPECT_EQ(calls, 0);
	EXPECT_EQ(root.getExportedBonusList().size(), 1);
}

TEST_F(ProjectedBonusCleanupTest, UnknownRootCannotRecordAdvancingInvalidation)
{
	graph.capture(root);
	CBonusSystemNode foreign{BonusNodeType::PLAYER};
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.nodeHasChanged(foreign), std::out_of_range);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.query(root, Selector::all, false), std::logic_error);
}

TEST_F(ProjectedBonusCleanupTest, MalformedQueryOnlyChildCycleRefusesInsteadOfRecursingForever)
{
	graph.capture(root);
	auto malformed = graph.values(root);
	malformed.children.push_back(&root);
	graph.replace(root, std::move(malformed));
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.removeBonusesRecursive(root, Selector::all), std::logic_error);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.query(root, Selector::all, false), std::logic_error);
}

class ProjectedUnitCleanupEnvironmentTest : public Test, protected environmentFixture::Projection {};

TEST_F(ProjectedUnitCleanupEnvironmentTest, RecursiveCleanupFeedsLaterProjectedOwnershipAndBloodrageQueries)
{
	initialize();
	auto hypnosis = environmentFixture::bonus(BonusType::HYPNOTIZED, 1);
	hypnosis->duration = BonusDuration::UNTIL_BEING_ATTACKED;
	graph.addExported(subject, hypnosis);
	ASSERT_EQ(environment->unitEffectiveOwner(units[0].get()), PlayerColor(1));
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	graph.removeBonusesRecursive(subject, Bonus::UntilBeingAttacked);
	graph.nodeHasChanged(subject);
	EXPECT_EQ(environment->unitEffectiveOwner(units[0].get()), PlayerColor(0));
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 3);
	EXPECT_EQ(callback.battleGetOwner(&subject), PlayerColor(0));
	EXPECT_FALSE(subject.isHypnotized());
}
}
