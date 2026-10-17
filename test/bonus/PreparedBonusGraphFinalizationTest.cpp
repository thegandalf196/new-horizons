/*
 * PreparedBonusGraphFinalizationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <new>
#include <stdexcept>
#include <type_traits>

namespace test
{
using namespace ::testing;
namespace
{
class AllocationRejectingUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> &, const BonusEvaluationContext &) const override
	{
		throw std::bad_alloc();
	}
};
}

class PreparedBonusGraphFinalizationTest : public Test
{
protected:
	CBonusSystemNode root{BonusNodeType::PLAYER}, child{BonusNodeType::ARMY};
	CBonusSystemNode leaf{BonusNodeType::STACK_BATTLE}, other{BonusNodeType::ARMY};
	ProjectedBonusGraph graph;
	TNodesVector authority{&root, &child, &leaf, &other};
	std::shared_ptr<Bonus> speed(int amount)
	{
		return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
			BonusSource::OTHER, amount, BonusSourceID());
	}
	void SetUp() override
	{
		child.attachTo(root);
		leaf.attachTo(child);
		other.attachTo(root);
		root.addNewBonus(speed(2));
		child.addNewBonus(speed(3));
		graph.capture(child);
	}
	static std::vector<std::shared_ptr<Bonus>> pointers(const BonusList & list)
	{
		return {list.begin(), list.end()};
	}
};

TEST_F(PreparedBonusGraphFinalizationTest, WarmDependentCommitRetainsPointersAndIsSingleUseNoThrow)
{
	const auto retained = leaf.getAllBonuses(Selector::all, "finalization");
	const auto retainedPointers = pointers(*retained);
	const auto before = pointers(root.getExportedBonusList());
	const auto parents = child.getParentNodes();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	auto added = speed(9);
	graph.addExported(root, added);
	PreparedBonusGraphFinalization plan(graph, authority);
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	EXPECT_EQ(retained->totalValue(), 5);
	ASSERT_TRUE(plan.readyToCommit());
	static_assert(noexcept(plan.commit()));
	static_assert(!std::is_move_constructible_v<PreparedBonusGraphFinalization>);
	static_assert(!std::is_copy_constructible_v<PreparedBonusGraphFinalization>);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "finalization")->totalValue(), 14);
	EXPECT_EQ(retained->totalValue(), 5);
	EXPECT_EQ(pointers(*retained), retainedPointers);
	EXPECT_EQ(root.getExportedBonusList().back(), added);
	EXPECT_EQ(child.getParentNodes(), parents);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 1);
	EXPECT_FALSE(plan.commit());
	EXPECT_THROW(graph.query(child, Selector::all, false), std::logic_error);
}

TEST_F(PreparedBonusGraphFinalizationTest, RecorderCapturesDepthFirstChildOrderAndSameGenerationEarlyReturn)
{
	ProjectedBonusInvalidations journal;
	journal.record(true, &root, graph);
	journal.record(false, &root, graph);
	const auto & events = journal.recordedEvents();
	ASSERT_EQ(events.size(), 2);
	EXPECT_TRUE(events[0].advance);
	EXPECT_EQ(events[0].targets, (TCNodesVector{&root, &child, &leaf, &other}));
	EXPECT_FALSE(events[1].advance);
	EXPECT_TRUE(events[1].targets.empty());
	root.nodeHasChanged();
	ProjectedBonusInvalidations borrowed;
	borrowed.record(false, &root, graph);
	ASSERT_EQ(borrowed.recordedEvents().size(), 1);
	EXPECT_TRUE(borrowed.recordedEvents()[0].targets.empty());
}

TEST_F(PreparedBonusGraphFinalizationTest, NetZeroListsStillCommitBothOrderedGenerationEvents)
{
	const auto before = pointers(root.getExportedBonusList());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	auto temporary = speed(9);
	graph.addExported(root, temporary);
	graph.removeExported(root, temporary);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 2);
}

TEST_F(PreparedBonusGraphFinalizationTest, AuthorityAndUnjournaledReplacementFailBeforeLogicalMutation)
{
	const auto before = pointers(root.getExportedBonusList());
	graph.addExported(root, speed(9));
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&root})), std::invalid_argument);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&root, &child, &leaf, &root})), std::invalid_argument);
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	auto raw = graph.values(root);
	graph.replace(root, raw);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, authority)), std::logic_error);
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
}

TEST_F(PreparedBonusGraphFinalizationTest, StaleLaterNodeAndUnrelatedGenerationRefuseBeforeFirstWrite)
{
	const auto before = pointers(root.getExportedBonusList());
	graph.addExported(root, speed(9));
	PreparedBonusGraphFinalization plan(graph, authority);
	other.nodeHasChanged();
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	ProjectedBonusGraph fresh;
	fresh.capture(child);
	fresh.addExported(root, speed(9));
	PreparedBonusGraphFinalization second(fresh, authority);
	CBonusSystemNode unrelated(BonusNodeType::PLAYER);
	unrelated.nodeHasChanged();
	EXPECT_FALSE(second.commit());
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
}

TEST_F(PreparedBonusGraphFinalizationTest, OrdinaryAddAndPropagationOwnSeparateFreshGenerations)
{
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.addExported(root, speed(1));
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	graph.addExported(child, aura);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(root.valOfBonuses(BonusType::STACKS_SPEED), 10);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 2);
}

TEST_F(PreparedBonusGraphFinalizationTest, PropagationOwnsFreshGenerationAcrossWarmDependentCollision)
{
	// root has an older stamp, child/leaf own the current global generation.
	const auto held = leaf.getAllBonuses(Selector::all, "propagated-collision");
	ASSERT_EQ(held->totalValue(), 5);
	const auto before = pointers(root.getExportedBonusList());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	graph.addExported(child, aura);
	PreparedBonusGraphFinalization plan(graph, authority);
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "propagated-collision")->totalValue(), 5);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(leaf.getAllBonuses(Selector::all, "propagated-collision")->totalValue(), 12);
	EXPECT_EQ(held->totalValue(), 5);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 1);
}

TEST_F(PreparedBonusGraphFinalizationTest, PropagatedAddAndRemovalKeepNativeAndPreparedGenerationDeltasEqual)
{
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	const auto initial = PreparedBonusGraphStorage::currentGeneration();
	graph.addExported(child, aura);
	graph.removeExported(child, aura);
	PreparedBonusGraphFinalization plan(graph, authority);
	ASSERT_TRUE(plan.commit());
	const auto preparedDelta = uint32_t(PreparedBonusGraphStorage::currentGeneration()) - uint32_t(initial);
	const auto nativeInitial = PreparedBonusGraphStorage::currentGeneration();
	child.addNewBonus(aura);
	ASSERT_EQ(leaf.valOfBonuses(BonusType::STACKS_SPEED), 12);
	child.removeBonus(aura);
	EXPECT_EQ(leaf.valOfBonuses(BonusType::STACKS_SPEED), 5);
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()) - uint32_t(nativeInitial), preparedDelta);
	EXPECT_EQ(preparedDelta, 2);
}

TEST_F(PreparedBonusGraphFinalizationTest, AllocationRejectionDuringProjectionCannotExposeLivePrefix)
{
	const auto before = pointers(root.getExportedBonusList());
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	aura->propagationUpdater = std::make_shared<AllocationRejectingUpdater>();
	EXPECT_THROW(graph.addExported(child, aura), std::bad_alloc);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, authority)), std::logic_error);
	EXPECT_EQ(pointers(root.getExportedBonusList()), before);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 5);
}
}
