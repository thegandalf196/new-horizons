/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/battle/OwnedFrameRelationCapture.h"
#include <stdexcept>
namespace
{
using Graph = OwnedRecipientBonusGraph;
using Relation = Graph::SourceRelation;
Graph::NodeInput stack()
{
	Graph::NodeInput value;
	value.facts.nodeType = BonusNodeType::STACK_BATTLE;
	value.facts.stackCount = 2;
	value.facts.stableStackLevel = 7;
	return value;
}
OwnedBonusEvaluationFacts source()
{
	OwnedBonusEvaluationFacts value;
	value.nodeType = BonusNodeType::CREATURE;
	value.owner = PlayerColor(2);
	return value;
}
std::shared_ptr<Bonus> effect(int value)
{
	auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
	result->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
	return result;
}
CSelector all() { return CSelector([](const Bonus *) { return true; }); }
std::vector<std::unique_ptr<Graph>> prepare(std::vector<Graph::CaptureInput> inputs,
	std::vector<std::vector<Relation>> relations)
{
	return Graph::fromOwnedBatchInputs(std::move(inputs), std::move(relations), OwnedBonusCaptureCodec::current());
}
class CountingPropagator final : public IPropagator
{
	int & calls;
public:
	explicit CountingPropagator(int & calls) : calls(calls) {}
	bool shouldBeAttached(CBonusSystemNode *) const override { ++calls; return true; }
};
}
TEST(OwnedSourceRelationRecipeTest, SharedUpdaterRemovalUsesOnePoolAndKeepsSiblingRawGraph)
{
	auto exported = effect(2);
	exported->propagationUpdater = std::make_shared<OwnerUpdater>();
	auto arrival = effect(9); arrival->propagationUpdater = exported->propagationUpdater;
	auto unrelated = effect(11); unrelated->propagationUpdater = std::make_shared<OwnerUpdater>();
	auto left = stack(); left.local = {arrival, unrelated};
	auto right = stack(); right.local = {arrival};
	const auto before = exported->toJsonNode(), arrivalBefore = arrival->toJsonNode();
	auto graphs = prepare({{{left}, 0, {exported}}, {{right}, 0, {exported}}},
		{{Relation{false, source(), {2}, {0}}}, {}});
	ASSERT_EQ(graphs[0]->local(graphs[0]->root()).size(), 1u);
	EXPECT_EQ(graphs[0]->local(graphs[0]->root()).front()->val, 11);
	ASSERT_EQ(graphs[1]->local(graphs[1]->root()).size(), 1u);
	EXPECT_EQ(graphs[1]->local(graphs[1]->root()).front()->val, 9);
	EXPECT_EQ(graphs[0]->payload(graphs[0]->extraRecord(0)), graphs[1]->payload(graphs[1]->extraRecord(0)));
	EXPECT_EQ(exported->toJsonNode(), before); EXPECT_EQ(arrival->toJsonNode(), arrivalBefore);
}
TEST(OwnedSourceRelationRecipeTest, RecordAndRepeatedDestinationOrderRemainCanonical)
{
	auto first = effect(3), second = effect(5);
	auto root = stack();
	auto graphs = prepare({{{root}, 0, {first, second}}}, {{Relation{true, source(), {1, 0}, {0, 0}}}});
	const auto & local = graphs[0]->local(graphs[0]->root());
	ASSERT_EQ(local.size(), 4u);
	auto it = local.begin();
	EXPECT_EQ((*it++)->val, 5); EXPECT_EQ((*it++)->val, 5);
	EXPECT_EQ((*it++)->val, 3); EXPECT_EQ((*it)->val, 3);
	EXPECT_EQ(graphs[0]->capturedRootQuery().records, graphs[0]->rootLocalRecords());
	EXPECT_EQ(graphs[0]->payload(graphs[0]->extraRecord(1)).get(), local.front().get());
}
TEST(OwnedSourceRelationRecipeTest, ImmediateSourceFactsApplyToEveryAncestorExportAndRemovalNeverUpdates)
{
	auto first = effect(3), inherited = effect(5);
	first->propagationUpdater = inherited->propagationUpdater = std::make_shared<OwnerUpdater>();
	auto root = stack();
	auto graphs = prepare({{{root}, 0, {first, inherited}}}, {{Relation{true, source(), {0, 1}, {0}}}});
	const auto & values = graphs[0]->local(graphs[0]->root());
	ASSERT_EQ(values.size(), 2u);
	for(const auto & value : values) EXPECT_EQ(value->bonusOwner, PlayerColor(2));
	// Missing applicable updater facts are legal for removal: it only uses the
	// predicate and raw/updater alias identity, never createUpdatedBonus.
	auto removed = std::make_shared<Bonus>(*first);
	removed->propagationUpdater = std::make_shared<TimesHeroLevelUpdater>();
	root.local = {removed};
	EXPECT_NO_THROW(prepare({{{root}, 0, {removed}}}, {{Relation{false, source(), {1}, {0}}}}));
}
TEST(OwnedSourceRelationRecipeTest, FullAndNativeRecipesKeepDistinctUpstreamArrivals)
{
	auto old = effect(3), fresh = effect(5);
	auto full = stack(), native = stack(), parent = stack();
	full.parents = native.parents = {1}; parent.local = {old};
	auto graphs = prepare({{{full, parent}, 0, {old, fresh}}, {{native, parent}, 0, {old, fresh}}},
		{{Relation{false, source(), {1}, {1}}, Relation{true, source(), {2}, {0, 1}}}, {}});
	EXPECT_EQ(graphs[0]->collect().valOfBonuses(all()), 10);
	EXPECT_EQ(graphs[1]->collect().valOfBonuses(all()), 3);
	EXPECT_TRUE(graphs[1]->rootLocalRecords().empty());
	EXPECT_EQ(graphs[0]->capturedRootQuery().parents, std::vector<size_t>{1});
}
TEST(OwnedSourceRelationRecipeTest, UnknownNativePredicateIsRejectedWithoutInvocationOrGenerationChange)
{
	int calls = 0; auto raw = effect(3); raw->propagator = std::make_shared<CountingPropagator>(calls);
	auto root = stack();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(prepare({{{root}, 0, {raw}}}, {{Relation{true, source(), {0}, {0}}}}), std::invalid_argument);
	EXPECT_EQ(calls, 0); EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
TEST(OwnedSourceRelationRecipeTest, CaptureReplayAndRetirementKeepSourceAndGlobalGenerationExact)
{
	auto raw = effect(3); raw->propagationUpdater = std::make_shared<OwnerUpdater>();
	const auto before = raw->toJsonNode(); auto root = stack();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	std::weak_ptr<Bonus> retired;
	{
		auto graphs = prepare({{{root}, 0, {raw}}}, {{Relation{true, source(), {0}, {0}}}});
		retired = graphs[0]->payload(0);
		EXPECT_NE(retired.lock().get(), raw.get());
		EXPECT_EQ(graphs[0]->collect().size(), 1u);
	}
	EXPECT_TRUE(retired.expired()); EXPECT_EQ(raw->toJsonNode(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
TEST(OwnedSourceRelationRecipeTest, InvalidRoutesOrHandlesRefuseBeforeAnyPredicate)
{
	auto raw = effect(3); auto root = stack();
	EXPECT_THROW(prepare({{{root}, 0, {raw}}}, {{Relation{true, source(), {99}, {0}}}}), std::invalid_argument);
	EXPECT_THROW(prepare({{{root}, 0, {raw}}}, {{Relation{true, source(), {0}, {99}}}}), std::invalid_argument);
	root.facts.nodeType = BonusNodeType::CREATURE;
	EXPECT_THROW(prepare({{{root}, 0, {raw}}}, {{Relation{true, source(), {0}, {0}}}}), std::invalid_argument);
}

TEST(OwnedSourceRelationRecipeTest, NativeRecipeCaptureReadsOrderedExportsWithoutInvokingNativePredicate)
{
	int calls = 0;
	CBonusSystemNode recipient(BonusNodeType::STACK_BATTLE), parent(BonusNodeType::ARMY);
	CBonusSystemNode creature(BonusNodeType::CREATURE), ancestor(BonusNodeType::ARTIFACT);
	recipient.attachTo(parent); creature.attachTo(ancestor);
	auto first = effect(3), inherited = effect(5);
	first->propagator = inherited->propagator = std::make_shared<CountingPropagator>(calls);
	// Raw export fixture setup intentionally avoids exportBonus: this is the
	// unknown/custom predicate whose capture must NEVER evaluate it.
	creature.getExportedBonusList().push_back(first);
	ancestor.getExportedBonusList().push_back(inherited);
	auto root = stack(); root.parents = {1};
	Graph::NodeInput army; army.facts.nodeType = BonusNodeType::ARMY; army.facts.armySlots.emplace();
	Graph::CaptureInput input{{root, army}, 0, {}};
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto firstBefore = first->val, inheritedBefore = inherited->val;
	const auto recipe = battle::ownedFrameCapture::captureFormRelation(recipient, creature, true,
		source(), {&recipient, &parent}, input);
	EXPECT_EQ(recipe.records, (std::vector<size_t>{0, 1}));
	EXPECT_EQ(recipe.destinations, (std::vector<size_t>{0, 1}));
	ASSERT_EQ(input.retainedRecords.size(), 2u);
	EXPECT_EQ(input.retainedRecords[0].get(), first.get()); EXPECT_EQ(input.retainedRecords[1].get(), inherited.get());
	EXPECT_EQ(calls, 0); EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(first->val, firstBefore); EXPECT_EQ(inherited->val, inheritedBefore);
	EXPECT_THROW(prepare({input}, {{recipe}}), std::invalid_argument);
	EXPECT_EQ(calls, 0); // Rejection also precedes the custom predicate.
	creature.getExportedBonusList().clear(); ancestor.getExportedBonusList().clear();
}
