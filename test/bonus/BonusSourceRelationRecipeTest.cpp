/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusSourceRelationRecipe.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <map>
#include <stdexcept>
namespace
{
std::shared_ptr<Bonus> record(int value)
{
	return std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
}
class RelationGraph final : public BonusGraphQuery
{
	BonusList empty;
public:
	std::map<const CBonusSystemNode *, TCNodesVector> edges;
	std::map<const CBonusSystemNode *, BonusList> exports;
	const BonusList & local(const CBonusSystemNode &) const override { return empty; }
	const TCNodesVector & parents(const CBonusSystemNode & node) const override { return edges.at(&node); }
	const TNodesVector & propagatingParents(const CBonusSystemNode &) const override
	{
		throw std::logic_error("Source recipe must not walk propagation destinations");
	}
	const TNodesVector & children(const CBonusSystemNode &) const override
	{
		throw std::logic_error("Creature source recipe must not walk children");
	}
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override { return node; }
};
class RecordingUpdater final : public IUpdater
{
public:
	mutable int calls = 0;
	mutable const CBonusSystemNode * last = nullptr;
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		++calls; last = &context.originalNode();
		return std::make_shared<Bonus>(*bonus);
	}
};
TEST(BonusSourceRelationRecipeTest, SourceThenCanonicalAncestorOrderKeepsImmediateContextWithoutEdgesOrChildReads)
{
	CBonusSystemNode source(BonusNodeType::CREATURE), first(BonusNodeType::CREATURE), second(BonusNodeType::CREATURE);
	CBonusSystemNode recipient(BonusNodeType::HERO);
	RelationGraph graph;
	graph.edges[&source] = {&second, &first}; graph.edges[&first] = {}; graph.edges[&second] = {};
	auto updater = std::make_shared<RecordingUpdater>();
	for(auto * node : {&source, &first, &second})
	{
		auto bonus = record(node == &source ? 1 : node == &first ? 2 : 3);
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
		bonus->propagationUpdater = updater;
		graph.exports[node].push_back(bonus);
		graph.exports[node].push_back(record(99)); // Nonpropagating exports are not a relation arrival.
	}
	std::vector<std::shared_ptr<Bonus>> visited;
	BonusList accepted;
	const auto version = source.getTreeVersion();
	forEachPropagatingSourceRecord(graph, source,
		[&](const CBonusSystemNode & node) -> const BonusList & { return graph.exports.at(&node); },
		[&](const auto & bonus)
		{
			visited.push_back(bonus);
			EXPECT_NE(appendPropagatedArrival(accepted, bonus, recipient, source), nullptr);
			EXPECT_EQ(updater->last, &source);
		});
	ASSERT_EQ(visited.size(), 3u); EXPECT_EQ(visited.front(), graph.exports.at(&source).front());
	const TCNodes ordered{&first, &second}; size_t index = 1;
	for(const auto * node : ordered) EXPECT_EQ(visited.at(index++), graph.exports.at(node).front());
	EXPECT_EQ(updater->calls, 3); EXPECT_EQ(accepted.size(), 3u);
	EXPECT_EQ(source.getTreeVersion(), version); EXPECT_TRUE(source.getParentNodes().empty());
}
TEST(BonusSourceRelationRecipeTest, RemovalUsesSharedUpdaterIdentityAndNeverInvokesIt)
{
	CBonusSystemNode source(BonusNodeType::CREATURE), recipient(BonusNodeType::HERO);
	auto updater = std::make_shared<RecordingUpdater>();
	auto first = record(1), second = record(2), plain = record(3), different = record(3);
	for(auto bonus : {first, second})
	{
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
		bonus->propagationUpdater = updater;
	}
	plain->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
	BonusList accepted;
	appendPropagatedArrival(accepted, first, recipient, source);
	appendPropagatedArrival(accepted, second, recipient, source);
	accepted.push_back(plain); accepted.push_back(different);
	ASSERT_EQ(updater->calls, 2);
	const auto erased = removePropagatedArrival(accepted, first, recipient);
	EXPECT_TRUE(erased.accepted); EXPECT_TRUE(erased.removed); EXPECT_EQ(updater->calls, 2);
	ASSERT_EQ(accepted.size(), 2u); EXPECT_EQ(accepted.front(), plain); EXPECT_EQ(accepted.back(), different);
	const auto exact = removePropagatedArrival(accepted, plain, recipient);
	EXPECT_TRUE(exact.accepted); EXPECT_TRUE(exact.removed);
	ASSERT_EQ(accepted.size(), 1u); EXPECT_EQ(accepted.front(), different);
	const auto absent = removePropagatedArrival(accepted, plain, recipient);
	EXPECT_TRUE(absent.accepted); EXPECT_FALSE(absent.removed); EXPECT_EQ(updater->calls, 2);
}
TEST(BonusSourceRelationRecipeTest, RejectedDestinationNeitherUpdatesNorEditsAcceptedRecords)
{
	CBonusSystemNode source(BonusNodeType::CREATURE), recipient(BonusNodeType::CREATURE);
	auto updater = std::make_shared<RecordingUpdater>(); auto bonus = record(1);
	bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
	bonus->propagationUpdater = updater;
	BonusList accepted; accepted.push_back(bonus);
	EXPECT_EQ(appendPropagatedArrival(accepted, bonus, recipient, source), nullptr);
	const auto erased = removePropagatedArrival(accepted, bonus, recipient);
	EXPECT_FALSE(erased.accepted); EXPECT_FALSE(erased.removed);
	EXPECT_EQ(updater->calls, 0); ASSERT_EQ(accepted.size(), 1u); EXPECT_EQ(accepted.front(), bonus);
}
}
