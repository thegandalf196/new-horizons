/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include <stdexcept>
namespace
{
using Input = OwnedRecipientBonusGraph::NodeInput;
Input hero(int level)
{
	Input input;
	input.facts.nodeType = BonusNodeType::HERO;
	input.facts.heroLevel = level;
	input.facts.armySlots.emplace();
	return input;
}
std::shared_ptr<Bonus> speed(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
}
CSelector all() { return CSelector([](const Bonus *) { return true; }); }
}
TEST(OwnedRecipientBonusGraphTest, DiamondVisitsEveryCanonicalUpdaterPathInParentOrder)
{
	std::vector<Input> nodes(4);
	nodes[0].parents = {1, 2};
	nodes[1] = hero(3);
	nodes[2] = hero(5);
	nodes[1].parents = {3};
	nodes[2].parents = {3};
	auto raw = speed(2);
	raw->updater = std::make_shared<TimesHeroLevelUpdater>();
	nodes[3].local = {raw};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(nodes, 0);
	raw->val = 90;
	const auto collected = graph->collect();
	ASSERT_EQ(collected.size(), 2u);
	auto it = collected.begin();
	EXPECT_EQ((*it++)->val, 6);
	EXPECT_EQ((*it)->val, 10);
	EXPECT_EQ(graph->collect().valOfBonuses(all()), 16);
}
TEST(OwnedRecipientBonusGraphTest, RepeatedParentIsNotOwnershipDeduplicatedOutOfQuery)
{
	std::vector<Input> nodes(2);
	nodes[0].parents = {1, 1};
	nodes[1] = hero(3);
	auto raw = speed(2);
	raw->updater = std::make_shared<TimesHeroLevelUpdater>();
	nodes[1].local = {raw};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(std::move(nodes), 0);
	const auto collected = graph->collect();
	ASSERT_EQ(collected.size(), 2u);
	for(const auto & bonus : collected) EXPECT_EQ(bonus->val, 6);
}
TEST(OwnedRecipientBonusGraphTest, CanonicalFixedPointAcceptsInitiallyUndecidedDependency)
{
	Input root;
	auto dependent = speed(9);
	dependent->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::STACK_HEALTH);
	auto prerequisite = speed(3);
	prerequisite->type = BonusType::STACK_HEALTH;
	root.local = {dependent, prerequisite};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0);
	const auto accepted = graph->query(all(), true);
	ASSERT_EQ(accepted.size(), 2u);
	EXPECT_EQ((*accepted.begin())->type, BonusType::STACK_HEALTH);
	EXPECT_EQ(accepted.valOfBonuses(Selector::type()(BonusType::STACKS_SPEED)), 9);
}
TEST(OwnedRecipientBonusGraphTest, CanonicalStackingPrecedesRequestedSelector)
{
	Input root;
	auto low = speed(5);
	auto high = speed(20);
	low->stacking = high->stacking = "owned-storage-order-control";
	root.local = {low, high};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0);
	const CSelector lowOnly([](const Bonus * b) { return b->val < 10; });
	EXPECT_TRUE(graph->query(lowOnly).empty());
	ASSERT_EQ(graph->query(lowOnly, true).size(), 1u);
	EXPECT_EQ(graph->query(lowOnly, true).valOfBonuses(all()), 5);
}
TEST(OwnedRecipientBonusGraphTest, CanonicalInheritedPrimaryRatingPassRemainsInPlace)
{
	std::vector<Input> nodes(2);
	nodes[0].parents = {1};
	nodes[1] = hero(3);
	nodes[1].primaryRating = true;
	auto rating = speed(2);
	rating->type = BonusType::PRIMARY_SKILL;
	rating->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	rating->updater = std::make_shared<TimesHeroLevelUpdater>();
	auto ordinary = speed(3);
	ordinary->updater = rating->updater;
	nodes[1].local = {rating, ordinary};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(std::move(nodes), 0);
	const auto collected = graph->collect();
	ASSERT_EQ(collected.size(), 1u);
	EXPECT_EQ((*collected.begin())->type, BonusType::STACKS_SPEED);
	EXPECT_EQ((*collected.begin())->val, 9);
}
TEST(OwnedRecipientBonusGraphTest, RejectsCyclesInvalidParentsAndUnrelatedNodesBeforeQuery)
{
	Input root;
	root.parents = {0};
	EXPECT_THROW(OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0), std::invalid_argument);
	root.parents = {2};
	EXPECT_THROW(OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0), std::invalid_argument);
	root.parents.clear();
	EXPECT_THROW(OwnedRecipientBonusGraph::fromOwnedInputs({root, Input{}}, 0), std::invalid_argument);
	EXPECT_THROW(OwnedRecipientBonusGraph::fromOwnedInputs({}, 0), std::invalid_argument);
}
TEST(OwnedRecipientBonusGraphTest, HasNoNativeAttachmentOrForeignStorageFallback)
{
	std::vector<Input> nodes(2);
	nodes[0].parents = {1};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(std::move(nodes), 0);
	EXPECT_EQ(graph->parents(graph->root()).size(), 1u);
	EXPECT_TRUE(graph->root().getParentNodes().empty());
	EXPECT_TRUE(graph->children(graph->root()).empty());
	EXPECT_TRUE(graph->propagatingParents(graph->root()).empty());
	CBonusSystemNode foreign(BonusNodeType::UNKNOWN, true);
	EXPECT_THROW(graph->local(foreign), std::invalid_argument);
	EXPECT_THROW(graph->parents(foreign), std::invalid_argument);
	EXPECT_THROW(graph->evaluation(foreign), std::invalid_argument);
}
TEST(OwnedRecipientBonusGraphTest, OwnedPayloadRetirementBreaksNestedNonRootCycles)
{
	auto raw = speed(1);
	auto first = speed(2);
	auto second = speed(3);
	auto connect = [](const auto & from, const auto & to)
	{
		BonusParametersOnCombatEvent effects;
		effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{to, false});
		from->parameters = std::make_shared<BonusParameters>(effects);
	};
	connect(raw, first);
	connect(first, second);
	connect(second, first);
	Input input;
	input.local = {raw};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
	std::weak_ptr<const Bonus> nested;
	{
		const auto & clone = *graph->local(graph->root()).begin();
		nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(
			clone->parameters->combatEffects()->effects.front()).bonus;
	}
	graph.reset();
	EXPECT_TRUE(nested.expired());
	raw->parameters.reset();
	first->parameters.reset();
	second->parameters.reset();
}
TEST(OwnedRecipientBonusGraphTest, InitialStaticFactsStageDoesNotPretendCreatureFactsAreBound)
{
	Input root;
	auto raw = speed(1);
	auto limiter = std::make_shared<CCreatureTypeLimiter>();
	limiter->creatureID = CreatureID(0);
	limiter->includeUpgrades = true;
	raw->limiter = limiter;
	root.local = {raw};
	std::unique_ptr<OwnedRecipientBonusGraph> captured;
	ASSERT_NO_THROW(captured = OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0));
	ASSERT_TRUE(captured);
	root.facts.creatureContext.emplace(); // Captured absence differs from an unknown domain.
	auto absent = OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0);
	root.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{CreatureID(0), 1, EAlignment::NEUTRAL};
	auto matching = OwnedRecipientBonusGraph::fromOwnedInputs({root}, 0);
	EXPECT_THROW(captured->query(all()), std::logic_error); // D2 checks the actual evaluation destination lazily.
	EXPECT_TRUE(absent->query(all()).empty());
	ASSERT_EQ(matching->query(all()).size(), 1u);
}

TEST(OwnedRecipientBonusGraphTest, ConstructQueryRepeatAndRetireDoNotInvalidateNativeGenerationOrCache)
{
	CBonusSystemNode sentinel(BonusNodeType::UNKNOWN);
	sentinel.addNewBonus(speed(7));
	const auto selector = all();
	const auto native = sentinel.getAllBonuses(selector, "owned-storage-generation-sentinel");
	ASSERT_EQ(native->valOfBonuses(selector), 7);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto version = sentinel.getTreeVersion();
	{
		std::vector<Input> nodes(2);
		nodes[0].parents = {1};
		nodes[1].local = {speed(3)};
		auto graph = OwnedRecipientBonusGraph::fromOwnedInputs(std::move(nodes), 0);
		EXPECT_EQ(graph->collect().valOfBonuses(selector), 3);
		EXPECT_EQ(graph->query(selector).valOfBonuses(selector), 3);
		EXPECT_EQ(graph->query(selector).valOfBonuses(selector), 3);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		EXPECT_EQ(sentinel.getTreeVersion(), version);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(sentinel.getTreeVersion(), version);
	EXPECT_EQ(sentinel.getAllBonuses(selector, "owned-storage-generation-sentinel"), native);
	EXPECT_EQ(native->valOfBonuses(selector), 7);
}
