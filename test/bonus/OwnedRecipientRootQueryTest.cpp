/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Graph = OwnedRecipientBonusGraph;
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
Graph::NodeInput root()
{
	Graph::NodeInput value;
	value.facts.nodeType = BonusNodeType::STACK_BATTLE;
	value.facts.stackCount = 4;
	value.facts.stableStackLevel = 5;
	value.facts.acceptance.emplace();
	return value;
}
std::shared_ptr<Bonus> bonus(BonusType type, int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}
CSelector all() { return CSelector([](const Bonus *) { return true; }); }
TLimiterPtr noLuck()
{
	return std::make_shared<NoneOfLimiter>(std::vector<TLimiterPtr>{std::make_shared<HasAnotherBonusLimiter>(BonusType::LUCK)});
}
}
TEST(OwnedRecipientRootQueryTest, RawMarkerRemovalReevaluatesPreviouslyInactiveConditionalRecords)
{
	auto input = root();
	auto marker = bonus(BonusType::LUCK, 1), rebirth = bonus(BonusType::REBIRTH, 100);
	rebirth->limiter = noLuck(); input.local = {rebirth, marker};
	auto graph = Graph::fromOwnedInputs({input}, 0);
	Graph::RootQuery present{input.facts, {}, graph->rootLocalRecords()};
	auto consumed = present; consumed.records = {0};
	WithoutLibrary absent;
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::REBIRTH), false, present).size(), 0u);
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::REBIRTH), false, consumed).valOfBonuses(all()), 100);
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::REBIRTH), false, present).size(), 0u);
	EXPECT_EQ(graph->local(graph->root()).size(), 2u); // Source storage/other query state never changed.
}
TEST(OwnedRecipientRootQueryTest, ExplicitFrameCountAndExtraRawRecordShareOneAliasClone)
{
	auto input = root(); auto marker = bonus(BonusType::LUCK, 1); input.local = {marker};
	auto casts = bonus(BonusType::CASTS, 99); casts->limiter = noLuck();
	casts->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, 7);
	auto graph = Graph::fromOwnedInputs({input}, 0, {casts, marker});
	ASSERT_EQ(graph->payloadCount(), 3u);
	EXPECT_EQ(graph->payload(0), graph->payload(2));
	Graph::RootQuery fixed{input.facts, {}, {1}};
	auto changed = fixed; changed.facts.stackCount = 0;
	casts->val = -400; marker->val = 400; // External source objects are independent.
	WithoutLibrary absent;
	EXPECT_EQ(graph->queryRoot(all(), true, fixed).valOfBonuses(all()), 28);
	EXPECT_EQ(graph->queryRoot(all(), true, changed).valOfBonuses(all()), 0);
	EXPECT_EQ(graph->queryRoot(all(), true, fixed).valOfBonuses(all()), 28);
}
TEST(OwnedRecipientRootQueryTest, SelectedFormParentsAndAcceptanceUseCanonicalRawGraphAndHeroStage)
{
	auto input = root(); input.parents = {1, 2, 3};
	Graph::NodeInput hero; hero.facts.nodeType = BonusNodeType::HERO; hero.facts.heroLevel = 3; hero.facts.armySlots.emplace();
	auto speed = bonus(BonusType::STACKS_SPEED, 2); speed->updater = std::make_shared<TimesHeroLevelUpdater>(); hero.local = {speed};
	Graph::NodeInput original, form; original.facts.nodeType = form.facts.nodeType = BonusNodeType::CREATURE;
	auto originalHP = bonus(BonusType::STACK_HEALTH, 10); originalHP->source = BonusSource::CREATURE_ABILITY; originalHP->sid = BonusSourceID(CreatureID(1)); original.local = {originalHP};
	auto formHP = bonus(BonusType::STACK_HEALTH, 1); formHP->source = BonusSource::CREATURE_ABILITY; formHP->sid = BonusSourceID(CreatureID(2)); form.local = {formHP};
	auto graph = Graph::fromOwnedInputs({input, hero, original, form}, 0);
	Graph::RootQuery initial{input.facts, {1, 2}, {}};
	auto transformed = initial; transformed.parents = {1, 2, 3};
	auto & acceptance = transformed.facts.acceptance->state;
	acceptance.hasForm = true; acceptance.currentForm = CreatureID(2); acceptance.originalForm = CreatureID(1);
	WithoutLibrary absent;
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::STACK_HEALTH), false, initial).valOfBonuses(all()), 10);
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::STACK_HEALTH), false, transformed).valOfBonuses(all()), 1);
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::STACKS_SPEED), false, transformed).valOfBonuses(all()), 6);
	EXPECT_EQ(graph->queryRoot(Selector::type()(BonusType::STACK_HEALTH), false, initial).valOfBonuses(all()), 10);
}
TEST(OwnedRecipientRootQueryTest, RepeatedParentVisitsRetainNonidempotentUpdaterStaging)
{
	auto input = root(); input.parents = {1, 1};
	Graph::NodeInput hero; hero.facts.nodeType = BonusNodeType::HERO; hero.facts.heroLevel = 3; hero.facts.armySlots.emplace();
	auto value = bonus(BonusType::STACKS_SPEED, 2); value->updater = std::make_shared<TimesHeroLevelUpdater>(); hero.local = {value};
	auto graph = Graph::fromOwnedInputs({input, hero}, 0);
	Graph::RootQuery query{input.facts, {1, 1}, {}};
	WithoutLibrary absent;
	const auto result = graph->queryRoot(all(), true, query);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result.valOfBonuses(all()), 12);
}
TEST(OwnedRecipientRootQueryTest, SourceStagesCannotBeFlattenedOrPromotedToRecipientRawRecords)
{
	auto input = root(); input.parents = {1}; input.local = {bonus(BonusType::LUCK, 1)};
	Graph::NodeInput parent, grandparent; parent.parents = {2}; parent.local = {bonus(BonusType::STACKS_SPEED, 2)};
	auto graph = Graph::fromOwnedInputs({input, parent, grandparent}, 0);
	Graph::RootQuery query{input.facts, {1}, graph->rootLocalRecords()};
	WithoutLibrary absent;
	auto promoted = query; promoted.records = {1};
	EXPECT_THROW(graph->queryRoot(all(), true, promoted), std::invalid_argument);
	auto skippedStage = query; skippedStage.parents = {2};
	EXPECT_THROW(graph->queryRoot(all(), true, skippedStage), std::invalid_argument);
	auto cyclic = query; cyclic.parents = {0};
	EXPECT_THROW(graph->queryRoot(all(), true, cyclic), std::invalid_argument);
	auto foreign = query; foreign.records = {99};
	EXPECT_THROW(graph->queryRoot(all(), true, foreign), std::out_of_range);
}
TEST(OwnedRecipientRootQueryTest, QueryAndOwnerRetirementDoNotMutateNativeGeneration)
{
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	std::weak_ptr<Bonus> weak;
	{
		auto input = root(); input.local = {bonus(BonusType::STACKS_SPEED, 3)};
		auto graph = Graph::fromOwnedInputs({input}, 0); weak = graph->payload(0);
		Graph::RootQuery query{input.facts, {}, graph->rootLocalRecords()};
		WithoutLibrary absent;
		{ const auto first = graph->queryRoot(all(), true, query); EXPECT_EQ(first.valOfBonuses(all()), 3); }
		EXPECT_EQ(graph->queryRoot(all(), true, query).valOfBonuses(all()), 3);
	}
	EXPECT_TRUE(weak.expired());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
TEST(OwnedRecipientRootQueryTest, ParentSubsequenceCannotInventDuplicateVisitsOrReverseCapturedOrder)
{
	auto input = root(); input.parents = {1, 2};
	Graph::NodeInput first, second;
	first.local = {bonus(BonusType::STACKS_SPEED, 1)};
	second.local = {bonus(BonusType::STACKS_SPEED, 2)};
	auto graph = Graph::fromOwnedInputs({input, first, second}, 0);
	Graph::RootQuery query{input.facts, {1, 2}, {}};
	WithoutLibrary absent;
	EXPECT_EQ(graph->queryRoot(all(), true, query).valOfBonuses(all()), 3);
	auto duplicate = query; duplicate.parents = {1, 1};
	EXPECT_THROW(graph->queryRoot(all(), true, duplicate), std::invalid_argument);
	auto reversed = query; reversed.parents = {2, 1};
	EXPECT_THROW(graph->queryRoot(all(), true, reversed), std::invalid_argument);
	auto subset = query; subset.parents = {2};
	EXPECT_EQ(graph->queryRoot(all(), true, subset).valOfBonuses(all()), 2);
}
TEST(OwnedRecipientRootQueryTest, ExtraAliasOfParentOnlyRecordCannotBypassItsUpdaterSourceStage)
{
	auto input = root(); input.parents = {1};
	Graph::NodeInput hero; hero.facts.nodeType = BonusNodeType::HERO;
	hero.facts.heroLevel = 3; hero.facts.armySlots.emplace();
	auto speed = bonus(BonusType::STACKS_SPEED, 2); speed->updater = std::make_shared<TimesHeroLevelUpdater>();
	hero.local = {speed};
	auto graph = Graph::fromOwnedInputs({input, hero}, 0, {speed});
	ASSERT_EQ(graph->payload(0), graph->payload(1)); // Same clone across parent/extra slots.
	Graph::RootQuery inherited{input.facts, {1}, {}};
	Graph::RootQuery promoted{input.facts, {}, {1}};
	WithoutLibrary absent;
	EXPECT_EQ(graph->queryRoot(all(), true, inherited).valOfBonuses(all()), 6);
	EXPECT_THROW(graph->queryRoot(all(), true, promoted), std::invalid_argument);
}
