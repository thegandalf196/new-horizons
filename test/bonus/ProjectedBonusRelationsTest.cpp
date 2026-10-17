/*
 * ProjectedBonusRelationsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <new>
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> relationBonus(int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, amount, BonusSourceID());
}

std::vector<int> values(const BonusList & list)
{
	std::vector<int> result;
	for(const auto & bonus : list)
		result.push_back(bonus->val);
	return result;
}

class SourceIdentityUpdater final : public IUpdater
{
	const CBonusSystemNode & expected;
public:
	explicit SourceIdentityUpdater(const CBonusSystemNode & expected) : expected(expected) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & source) const override
	{
		auto result = std::make_shared<Bonus>(*bonus);
		result->val += &source.originalNode() == &expected ? 100 : -100;
		return result;
	}
};

class RejectRelationUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> &,
		const BonusEvaluationContext &) const override { throw std::bad_alloc(); }
};
}

class ProjectedBonusRelationsTest : public Test
{
protected:
	CBonusSystemNode army{BonusNodeType::ARMY}, player{BonusNodeType::PLAYER};
	CBonusSystemNode creature{BonusNodeType::CREATURE}, artifact{BonusNodeType::ARTIFACT};
	ProjectedBonusGraph graph;
};

TEST_F(ProjectedBonusRelationsTest, FullAttachOwnsPrivateBlackRedAndChildEdgesBeforeAnyLiveMutation)
{
	player.addNewBonus(relationBonus(7));
	graph.attachTo(army, player);
	EXPECT_TRUE(army.getParentNodes().empty());
	EXPECT_TRUE(liveBonusGraph().children(player).empty());
	EXPECT_EQ(graph.parents(army), (TCNodesVector{&player}));
	EXPECT_EQ(graph.propagatingParents(army), (TNodesVector{&player}));
	EXPECT_EQ(graph.children(player), (TNodesVector{&army}));
	EXPECT_EQ(graph.query(army, Selector::all, false)->totalValue(), 7);
	army.attachTo(player);
	EXPECT_EQ(graph.parents(army), army.getParentNodes());
	EXPECT_EQ(graph.children(player), liveBonusGraph().children(player));
}

TEST_F(ProjectedBonusRelationsTest, SourceOnlyAttachHasNoPropagatingOrReciprocalChildEdge)
{
	creature.addNewBonus(relationBonus(4));
	graph.attachToSource(army, creature);
	EXPECT_EQ(graph.parents(army), (TCNodesVector{&creature}));
	EXPECT_TRUE(graph.propagatingParents(army).empty());
	EXPECT_TRUE(graph.children(creature).empty());
	EXPECT_EQ(graph.query(army, Selector::all, false)->totalValue(), 4);
	army.attachToSource(creature);
	EXPECT_EQ(graph.parents(army), army.getParentNodes());
	EXPECT_EQ(graph.children(creature), liveBonusGraph().children(creature));
}

TEST_F(ProjectedBonusRelationsTest, OrdinaryParentExportsUseAttachedNodeContextForLocalAndRedAncestors)
{
	auto local = relationBonus(1), inherited = relationBonus(2);
	for(auto bonus : {local, inherited})
	{
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
		bonus->propagationUpdater = std::make_shared<SourceIdentityUpdater>(army);
	}
	army.addNewBonus(local);
	artifact.addNewBonus(inherited);
	army.attachToSource(artifact);
	graph.attachTo(army, player);
	EXPECT_EQ(values(graph.values(player).accepted), (std::vector<int>{101, 102}));
	EXPECT_TRUE(liveBonusGraph().local(player).empty());
	army.attachTo(player);
	EXPECT_EQ(values(graph.values(player).accepted), values(liveBonusGraph().local(player)));
}

TEST_F(ProjectedBonusRelationsTest, SourceOnlyParentExportsUseParentContextForLocalAndRedAncestors)
{
	auto local = relationBonus(3), inherited = relationBonus(4);
	for(auto bonus : {local, inherited})
	{
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::ARMY);
		bonus->propagationUpdater = std::make_shared<SourceIdentityUpdater>(creature);
	}
	creature.addNewBonus(local);
	artifact.addNewBonus(inherited);
	creature.attachToSource(artifact);
	graph.attachToSource(army, creature);
	EXPECT_EQ(values(graph.values(army).accepted), (std::vector<int>{103, 104}));
	army.attachToSource(creature);
	EXPECT_EQ(values(graph.values(army).accepted), values(liveBonusGraph().local(army)));
}

TEST_F(ProjectedBonusRelationsTest, DetachAllPreservesFullParentThenRemainingSourceOrderAndExactGenerations)
{
	army.attachTo(player);
	army.attachToSource(creature);
	army.attachToSource(artifact);
	graph.capture(army);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.detachFromAll(army);
	EXPECT_TRUE(graph.parents(army).empty());
	EXPECT_TRUE(graph.propagatingParents(army).empty());
	EXPECT_TRUE(graph.children(player).empty());
	EXPECT_EQ(army.getParentNodes().size(), 3);
	PreparedBonusGraphFinalization plan(graph, {&army, &player, &creature, &artifact});
	ASSERT_TRUE(plan.commit());
	EXPECT_TRUE(army.getParentNodes().empty());
	EXPECT_TRUE(liveBonusGraph().children(player).empty());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 4);
}

TEST_F(ProjectedBonusRelationsTest, DetachRetainsUnchangedBonusPointerAndWarmResultAfterCommit)
{
	auto bonus = relationBonus(6);
	player.addNewBonus(bonus);
	army.attachTo(player);
	const auto retained = army.getAllBonuses(Selector::all, "relation-detach");
	graph.detachFrom(army, player);
	EXPECT_EQ(retained->totalValue(), 6);
	EXPECT_EQ(graph.query(army, Selector::all, false)->totalValue(), 0);
	PreparedBonusGraphFinalization plan(graph, {&army, &player});
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(army.getAllBonuses(Selector::all, "relation-detach")->totalValue(), 0);
	EXPECT_EQ((*retained)[0], bonus);
	EXPECT_EQ(player.getExportedBonusList().front(), bonus);
}

TEST_F(ProjectedBonusRelationsTest, HypotheticFullEdgeOmitsReciprocalChildAndPropagationLikeLiveOwner)
{
	CBonusSystemNode hypothetical(BonusNodeType::ARMY, true);
	graph.attachTo(hypothetical, player);
	EXPECT_TRUE(graph.children(player).empty());
	EXPECT_EQ(graph.parents(hypothetical), (TCNodesVector{&player}));
	EXPECT_EQ(graph.propagatingParents(hypothetical), (TNodesVector{&player}));
	hypothetical.attachTo(player);
	EXPECT_EQ(graph.children(player), liveBonusGraph().children(player));
	EXPECT_EQ(graph.parents(hypothetical), hypothetical.getParentNodes());
}

TEST_F(ProjectedBonusRelationsTest, PropagationAllocationFailurePoisonsPrivateEdgesOnly)
{
	auto bonus = relationBonus(1);
	bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	bonus->propagationUpdater = std::make_shared<RejectRelationUpdater>();
	army.addNewBonus(bonus);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.attachTo(army, player), std::bad_alloc);
	EXPECT_TRUE(army.getParentNodes().empty());
	EXPECT_TRUE(liveBonusGraph().children(player).empty());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.query(army, Selector::all, false), std::logic_error);
}

TEST_F(ProjectedBonusRelationsTest, CycleIsRefusedBeforePropagationAndLiveEdgesStayUnchanged)
{
	army.attachTo(player);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.attachTo(player, army), std::logic_error);
	EXPECT_TRUE(player.getParentNodes().empty());
	EXPECT_EQ(army.getParentNodes(), (TCNodesVector{&player}));
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedBonusRelationsTest, DuplicateAttachAndMissingDetachRefusePreparationNotLiveGameplay)
{
	army.attachTo(player);
	EXPECT_THROW(graph.attachTo(army, player), std::logic_error);
	ProjectedBonusGraph other;
	EXPECT_THROW(other.detachFrom(army, creature), std::logic_error);
	EXPECT_EQ(army.getParentNodes(), (TCNodesVector{&player}));
	ProjectedBonusGraph raw;
	raw.capture(army);
	auto malformed = raw.values(army);
	malformed.children.push_back(&army);
	raw.replace(army, std::move(malformed));
	EXPECT_THROW(raw.attachTo(army, creature), std::logic_error); // query-only raw graph, no ancestor walk
}

TEST_F(ProjectedBonusRelationsTest, PropagatedDetachRemovesMaterializedCopiesBeforeSourceAndChildEdges)
{
	auto bonus = relationBonus(1);
	bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::PLAYER);
	bonus->propagationUpdater = std::make_shared<SourceIdentityUpdater>(army);
	army.addNewBonus(bonus);
	army.attachTo(player);
	ASSERT_EQ(values(liveBonusGraph().local(player)), (std::vector<int>{101}));
	graph.detachFrom(army, player);
	EXPECT_TRUE(graph.values(player).accepted.empty());
	EXPECT_EQ(liveBonusGraph().local(player).size(), 1);
	army.detachFrom(player);
	EXPECT_TRUE(liveBonusGraph().local(player).empty());
	EXPECT_EQ(graph.children(player), liveBonusGraph().children(player));
	EXPECT_EQ(graph.parents(army), army.getParentNodes());
}
}
