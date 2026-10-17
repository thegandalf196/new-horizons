/*
 * ReadOnlyBonusGraphParticipantTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include <type_traits>

namespace test
{
namespace
{
std::shared_ptr<Bonus> speed(int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, amount, BonusSourceID());
}

class Source final : public CBonusSystemNode
{
public:
	Source() : CBonusSystemNode(BonusNodeType::CREATURE) { addNewBonus(speed(2)); }
};

struct StorageObservation
{
	const std::shared_ptr<Bonus> * local;
	const std::shared_ptr<Bonus> * exported;
	size_t localCapacity, exportedCapacity;
	const CBonusSystemNode * const * parents;
	CBonusSystemNode * const * children;
	CBonusSystemNode * const * propagating;
	size_t parentCapacity, childCapacity, propagatingCapacity;
	int32_t version;

	explicit StorageObservation(const CBonusSystemNode & node)
		: local(std::addressof(*liveBonusGraph().local(node).begin())),
		exported(std::addressof(*node.getExportedBonusList().begin())),
		localCapacity(liveBonusGraph().local(node).capacity()), exportedCapacity(node.getExportedBonusList().capacity()),
		parents(node.getParentNodes().data()), children(liveBonusGraph().children(node).data()),
		propagating(liveBonusGraph().propagatingParents(node).data()), parentCapacity(node.getParentNodes().capacity()),
		childCapacity(liveBonusGraph().children(node).capacity()),
		propagatingCapacity(liveBonusGraph().propagatingParents(node).capacity()), version(node.getTreeVersion()) {}

	void unchanged(const CBonusSystemNode & node) const
	{
		ASSERT_FALSE(liveBonusGraph().local(node).empty());
		ASSERT_FALSE(node.getExportedBonusList().empty());
		EXPECT_EQ(std::addressof(*liveBonusGraph().local(node).begin()), local);
		EXPECT_EQ(std::addressof(*node.getExportedBonusList().begin()), exported);
		EXPECT_EQ(liveBonusGraph().local(node).capacity(), localCapacity);
		EXPECT_EQ(node.getExportedBonusList().capacity(), exportedCapacity);
		EXPECT_EQ(node.getParentNodes().data(), parents);
		EXPECT_EQ(liveBonusGraph().children(node).data(), children);
		EXPECT_EQ(liveBonusGraph().propagatingParents(node).data(), propagating);
		EXPECT_EQ(node.getParentNodes().capacity(), parentCapacity);
		EXPECT_EQ(liveBonusGraph().children(node).capacity(), childCapacity);
		EXPECT_EQ(liveBonusGraph().propagatingParents(node).capacity(), propagatingCapacity);
		EXPECT_EQ(node.getTreeVersion(), version);
	}
};
}

TEST(ReadOnlyBonusGraphParticipantTest, ConstNativeSourceNeedsNoMutableCastOrContainerReservation)
{
	const Source source;
	CBonusSystemNode child(BonusNodeType::ARMY);
	child.attachToSource(source);
	const auto cached = source.getAllBonuses(Selector::all, "read-only-source");
	const StorageObservation observed(source);
	const auto original = source.getExportedBonusList().front();
	ProjectedBonusGraph graph;
	graph.capture(child);
	graph.addExported(child, speed(7));
	PreparedBonusGraphFinalization plan(graph, {&child}, {&source});
	observed.unchanged(source);
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	observed.unchanged(source);
	EXPECT_EQ(source.getExportedBonusList().front(), original);
	EXPECT_EQ(source.getAllBonuses(Selector::all, "read-only-source"), cached);
	EXPECT_EQ(child.getAllBonuses(Selector::all)->totalValue(), 9);
	EXPECT_FALSE(plan.commit());
}

TEST(ReadOnlyBonusGraphParticipantTest, ObservationOnlyPlanLeavesGenerationAndWarmNamedCacheUnchanged)
{
	const Source source;
	const auto cached = source.getAllBonuses(Selector::all, "observation-only");
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const StorageObservation observed(source);
	PreparedBonusGraphStorage plan;
	const auto index = plan.captureReadOnly(source);
	plan.replace(index, plan.projected(index));
	plan.prepare();
	ASSERT_TRUE(plan.commit());
	observed.unchanged(source);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(source.getAllBonuses(Selector::all, "observation-only"), cached);
}

TEST(ReadOnlyBonusGraphParticipantTest, EveryChangedStorageFieldRejectsReadOnlyLabelBeforePreparation)
{
	Source source;
	CBonusSystemNode other(BonusNodeType::ARMY);
	for(int field = 0; field < 5; ++field)
	{
		PreparedBonusGraphStorage plan;
		const auto index = plan.captureReadOnly(source);
		auto replacement = plan.projected(index);
		switch(field)
		{
		case 0: replacement.accepted.push_back(speed(9)); break;
		case 1: replacement.exported.clear(); break;
		case 2: replacement.parents.push_back(&other); break;
		case 3: replacement.propagatingParents.push_back(&other); break;
		case 4: replacement.children.push_back(&other); break;
		}
		const auto version = source.getTreeVersion();
		EXPECT_THROW(plan.replace(index, std::move(replacement)), std::invalid_argument) << field;
		EXPECT_EQ(source.getTreeVersion(), version);
		EXPECT_FALSE(plan.commit());
	}
}

TEST(ReadOnlyBonusGraphParticipantTest, ReadOnlyInvalidationTargetRefusesEvenWithUnchangedStorage)
{
	Source source;
	PreparedBonusGraphStorage plan;
	plan.captureReadOnly(source);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(plan.addInvalidation(true, {&source}), std::invalid_argument);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_FALSE(plan.commit());
}

TEST(ReadOnlyBonusGraphParticipantTest, ClassificationCannotBeSilentlyPromotedOrDowngraded)
{
	Source source;
	PreparedBonusGraphStorage observed;
	const auto read = observed.captureReadOnly(source);
	EXPECT_EQ(observed.captureReadOnly(source), read);
	EXPECT_THROW(observed.capture(source), std::invalid_argument);
	PreparedBonusGraphStorage writable;
	const auto edit = writable.capture(source);
	EXPECT_EQ(writable.capture(source), edit);
	EXPECT_THROW(writable.captureReadOnly(source), std::invalid_argument);
}

TEST(ReadOnlyBonusGraphParticipantTest, StaleReadOnlyRowRefusesBeforeEarlierMutableRowAndGraphCAS)
{
	Source source;
	CBonusSystemNode child(BonusNodeType::ARMY);
	PreparedBonusGraphStorage plan;
	const auto childIndex = plan.capture(child);
	auto replacement = plan.projected(childIndex);
	auto bonus = speed(9);
	replacement.accepted.push_back(bonus);
	replacement.exported.push_back(bonus);
	plan.replace(childIndex, replacement);
	plan.captureReadOnly(source);
	plan.addInvalidation(true, {&child});
	plan.prepare();
	source.nodeHasChanged();
	const auto afterExternalChange = PreparedBonusGraphStorage::currentGeneration();
	const auto childVersion = child.getTreeVersion();
	EXPECT_FALSE(plan.stillCurrent());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), afterExternalChange);
	EXPECT_EQ(child.getTreeVersion(), childVersion);
	EXPECT_TRUE(child.getExportedBonusList().empty());
}

TEST(ReadOnlyBonusGraphParticipantTest, NewlyWarmedReadOnlyCacheRefusesAtPrepareWithoutWrites)
{
	Source source;
	PreparedBonusGraphStorage plan;
	plan.captureReadOnly(source);
	const auto cached = source.getAllBonuses(Selector::all, "warmed-after-capture");
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(plan.prepare(), std::runtime_error);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(source.getAllBonuses(Selector::all, "warmed-after-capture"), cached);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST(ReadOnlyBonusGraphParticipantTest, ChangedReadOnlyPointerListRefusesFreshnessWithoutVersionAdvance)
{
	Source source;
	PreparedBonusGraphStorage plan;
	plan.captureReadOnly(source);
	plan.prepare();
	const auto version = source.getTreeVersion();
	source.getExportedBonusList()[0] = speed(9); // Actual legacy alias, not a sanctioned concurrent edit.
	EXPECT_EQ(source.getTreeVersion(), version);
	EXPECT_FALSE(plan.stillCurrent());
	EXPECT_FALSE(plan.commit());
}

TEST(ReadOnlyBonusGraphParticipantTest, MissingReadOnlyPartitionDoesNotRelaxExistingStrictConstructor)
{
	const Source source;
	CBonusSystemNode child(BonusNodeType::ARMY);
	child.attachToSource(source);
	ProjectedBonusGraph graph;
	graph.capture(child);
	graph.addExported(child, speed(7));
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&child})), std::invalid_argument);
	EXPECT_TRUE(child.getExportedBonusList().empty());
}

TEST(ReadOnlyBonusGraphParticipantTest, PartitionRejectsOverlapNullDuplicateAndUnknownRows)
{
	Source source;
	CBonusSystemNode child(BonusNodeType::ARMY), other(BonusNodeType::ARMY);
	child.attachToSource(source);
	ProjectedBonusGraph graph;
	graph.capture(child);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&child}, {&source, &child})), std::invalid_argument);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&child}, {nullptr})), std::invalid_argument);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&child}, {&source, &source})), std::invalid_argument);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {&child}, {&other})), std::invalid_argument);
}

TEST(ReadOnlyBonusGraphParticipantTest, FinalizerRejectsChangedPayloadOrTargetHiddenAsReadOnly)
{
	Source source;
	CBonusSystemNode child(BonusNodeType::ARMY);
	child.attachToSource(source);
	ProjectedBonusGraph changed;
	changed.capture(child);
	changed.addExported(source, speed(8));
	EXPECT_THROW((PreparedBonusGraphFinalization(changed, {&child}, {&source})), std::invalid_argument);
	ProjectedBonusGraph targeted;
	targeted.capture(child);
	targeted.nodeHasChanged(source);
	EXPECT_THROW((PreparedBonusGraphFinalization(targeted, {&child}, {&source})), std::invalid_argument);
	EXPECT_EQ(source.getExportedBonusList().size(), 1);
	EXPECT_TRUE(child.getExportedBonusList().empty());
}
}
