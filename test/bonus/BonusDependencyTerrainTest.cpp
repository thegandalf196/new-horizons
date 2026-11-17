/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyOverlay.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/BonusLimitation.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
namespace terrainDependencyTest
{
using namespace bonusDependencies;
const TerrainId GRASS{ETerrainId::GRASS};
const TerrainId DIRT{ETerrainId::DIRT};
std::shared_ptr<Bonus> reward(bool explicitTerrain = false)
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::TERRAIN_NATIVE, 1, BonusSourceID());
	auto limiter = std::make_shared<AllOfLimiter>();
	limiter->add(explicitTerrain ? std::make_shared<TerrainLimiter>(GRASS) : std::make_shared<TerrainLimiter>());
	limiter->add(std::make_shared<CreatureLevelLimiter>(1, 4));
	bonus->limiter = limiter;
	return bonus;
}
OwnedRecipientBonusGraph::NodeInput input(bool native = true)
{
	OwnedRecipientBonusGraph::NodeInput source;
	source.facts.nodeType = BonusNodeType::STACK_BATTLE;
	source.facts.owner = PlayerColor(0); source.facts.stackCount = 12;
	source.facts.stableStackLevel = 1;
	source.facts.creatureContext.emplace();
	source.facts.creatureContext->faction = FactionID(0);
	source.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{
		CreatureID(0), 1, EAlignment::GOOD};
	source.facts.acceptance.emplace();
	source.facts.terrainContext.emplace();
	source.facts.terrainContext->battle = GRASS;
	source.facts.terrainContext->legacyFallback = GRASS;
	source.facts.terrainContext->nativeMembership.emplace(GRASS, native);
	source.local = {reward()};
	return source;
}
QueryFrameBinding binding()
{
	battle::OwnedUnitCreatureResolver::Creature body;
	body.id = CreatureID(0); body.key = "certified-terrain-fixture";
	body.faction = FactionID(0); body.level = 1;
	QueryFrameBinding value;
	value.recipientId = 17; value.creature = body.id; value.count = 12;
	value.owner = PlayerColor(0); value.side = BattleSide::ATTACKER; value.slot = SlotID(0);
	value.profileOwner = std::make_shared<const battle::OwnedUnitCreatureResolver>(std::vector{body});
	return value;
}
void parity(const OwnedBonusQueryFrame & frame, const OwnedRecipientBonusGraph & native)
{
	for(bool unstacked : {false, true})
	{
		const auto expected = native.query(Selector::all, unstacked);
		const auto actual = frame.query(Selector::all, unstacked);
		ASSERT_EQ(actual->size(), expected.size());
		for(size_t i = 0; i < expected.size(); ++i)
		{
			EXPECT_EQ((*actual)[i]->type, expected[i]->type);
			EXPECT_EQ((*actual)[i]->val, expected[i]->val);
			EXPECT_EQ((*actual)[i]->bonusOwner, expected[i]->bonusOwner);
		}
	}
}
TEST(BonusDependencyTerrainTest, NativeAndNonNativeGrassUseSameNativeAllOfDecision)
{
	for(bool native : {false, true})
	{
		auto source = input(native);
		for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
		{
			auto bonus = reward(); bonus->type = BonusType::PRIMARY_SKILL;
			bonus->subtype = BonusSubtypeID(skill); source.local.push_back(bonus);
		}
		auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
		const auto context = binding();
		const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
		ASSERT_TRUE(program);
		const auto frame = program->refreshQueries(context, Change::None);
		ASSERT_TRUE(frame);
		ASSERT_NO_FATAL_FAILURE(parity(*frame, *graph));
		EXPECT_EQ(frame->getAllBonuses(Selector::all)->size(), native ? 3u : 0u);
		EXPECT_TRUE(frame->mayHaveBonusType(BonusType::STACKS_SPEED));
		auto changed = context; changed.count = 5;
		const auto casualty = program->refreshQueries(changed, Change::Count, frame);
		ASSERT_TRUE(casualty);
		ASSERT_NO_FATAL_FAILURE(parity(*casualty, *graph));
		EXPECT_TRUE(program->canRefresh(Change::Position));
		for(const auto dirty : {Change::TerrainContext, Change::Profile, Change::Controller,
			Change::ActiveEffects, Change::Roster, Change::Round})
		{
			EXPECT_FALSE(program->canRefresh(dirty));
			EXPECT_FALSE(program->refreshQueries(changed, dirty, casualty));
		}
	}
}
TEST(BonusDependencyTerrainTest, ExplicitTerrainAndTierLimitsKeepNativeSemantics)
{
	for(int level : {1, 3, 4})
	{
		auto source = input(false); source.local = {reward(true)};
		source.facts.creatureContext->creature->level = level;
		auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
		const auto context = binding();
		const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
		ASSERT_TRUE(program);
		const auto frame = program->refreshQueries(context, Change::None);
		ASSERT_TRUE(frame);
		ASSERT_NO_FATAL_FAILURE(parity(*frame, *graph));
		EXPECT_EQ(frame->getAllBonuses(Selector::all)->size(), level < 4 ? 1u : 0u);
	}
}
TEST(BonusDependencyTerrainTest, InheritedHeroExclusionKeepsActualPrimaryRatingSuppressionOrder)
{
	auto hero = input();
	hero.facts.nodeType = BonusNodeType::HERO;
	hero.facts.heroLevel = 3; hero.facts.armySlots.emplace();
	hero.facts.creatureContext->creature.reset(); hero.primaryRating = true;
	auto inherited = reward(); inherited->type = BonusType::PRIMARY_SKILL;
	inherited->subtype = BonusSubtypeID(PrimarySkill::ATTACK); hero.local = {inherited};
	auto recipient = input(); recipient.local.clear(); recipient.parents = {0};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({hero, recipient}, 1);
	const auto context = binding();
	const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
	ASSERT_TRUE(program);
	const auto frame = program->refreshQueries(context, Change::None);
	ASSERT_TRUE(frame);
	ASSERT_NO_FATAL_FAILURE(parity(*frame, *graph));
	EXPECT_EQ(frame->getAllBonuses(Selector::all)->size(), 1u);
}
TEST(BonusDependencyTerrainTest, StandaloneCaptureAndMissingTerrainDomainsStayClosed)
{
	const auto row = reward();
	EXPECT_FALSE(classify(*row).supported);
	auto bound = OwnedRecipientBonusGraph::fromOwnedInputs({input()}, 0);
	EXPECT_THROW(capture(row, {}, bound->evaluation(bound->root())), std::invalid_argument);
	auto source = input(); source.facts.terrainContext.reset();
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::logic_error);
	source = input(); source.facts.terrainContext->nativeMembership.clear();
	graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::logic_error);
	source = input(); source.facts.terrainContext->battle.reset();
	source.facts.terrainContext->legacyFallback.reset();
	graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::logic_error);
	source = input(); source.facts.creatureContext->faction.reset();
	graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::invalid_argument);
}
TEST(BonusDependencyTerrainTest, AdventureAndQueryTerrainPrecedenceRemainOwnedAcrossSourceRetirement)
{
	auto source = input();
	source.facts.terrainContext->adventure = DIRT;
	source.facts.terrainContext->query = GRASS;
	source.facts.terrainContext->nativeMembership.emplace(DIRT, false);
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	const auto context = binding();
	const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
	ASSERT_TRUE(program);
	const auto frame = program->refreshQueries(context, Change::None);
	ASSERT_TRUE(frame);
	ASSERT_NO_FATAL_FAILURE(parity(*frame, *graph));
	EXPECT_TRUE(frame->getAllBonuses(Selector::all)->empty());
	graph.reset(); source.local.clear(); source.facts.terrainContext.reset();
	auto count = context; count.count = 1;
	const auto surviving = program->refreshQueries(count, Change::Count, frame);
	ASSERT_TRUE(surviving);
	EXPECT_TRUE(surviving->getAllBonuses(Selector::all)->empty());
}
TEST(BonusDependencyTerrainTest, BaselessQueryTerrainPrecedesExplicitBattleFallback)
{
	for(bool query : {false, true})
	{
		auto source = input();
		auto & terrain = *source.facts.terrainContext;
		terrain.legacyFallback.reset(); terrain.explicitNativeContext = true;
		if(query) terrain.query = DIRT;
		terrain.nativeMembership.emplace(DIRT, false);
		auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
		const auto context = binding();
		const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
		ASSERT_TRUE(program);
		const auto frame = program->refreshQueries(context, Change::None);
		ASSERT_TRUE(frame);
		ASSERT_NO_FATAL_FAILURE(parity(*frame, *graph));
		EXPECT_EQ(frame->getAllBonuses(Selector::all)->size(), query ? 0u : 1u);
	}
}
class ForeignTerrain final : public TerrainLimiter
{
public:
	EDecision limit(const BonusLimitationContext &) const override
	{ throw std::logic_error("Unknown terrain implementation must not execute"); }
};
TEST(BonusDependencyTerrainTest, UnknownSubclassesAndNonAllOfTerrainChainsStayUnsupported)
{
	auto source = input(); auto unknown = reward();
	unknown->limiter = std::make_shared<ForeignTerrain>(); source.local = {unknown};
	CBonusSystemNode foreign(BonusNodeType::STACK_BATTLE);
	foreign.addNewBonus(unknown);
	EXPECT_THROW(captureNativeModifiers(liveBonusGraph(), foreign), std::invalid_argument);
	auto nested = std::make_shared<AllOfLimiter>(); nested->add(std::make_shared<ForeignTerrain>());
	unknown->limiter = nested;
	EXPECT_THROW(captureNativeModifiers(liveBonusGraph(), foreign), std::invalid_argument);
	auto alternatives = std::make_shared<AnyOfLimiter>(); alternatives->add(std::make_shared<TerrainLimiter>());
	unknown->limiter = alternatives;
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::invalid_argument);
}
TEST(BonusDependencyTerrainTest, DormantZeroNativeGroundGrantAndIncomingOverlayRefuseBeforeCollection)
{
	auto source = input();
	auto grant = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::TERRAIN_NATIVE,
		BonusSource::OTHER, 0, BonusSourceID());
	grant->limiter = std::make_shared<CreatureLevelLimiter>(99, 100);
	source.local.push_back(grant);
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_THROW(captureNativeModifiers(*graph, graph->root()), std::invalid_argument);
	source.local.pop_back(); graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	EXPECT_FALSE(supportsRecipientRecord(*grant));
	recipientBonusOverlay::Records added{grant}, updated;
	std::set<std::shared_ptr<Bonus>> removed;
	std::optional<recipientBonusOverlay::Values> stacked;
	std::optional<recipientBonusOverlay::Records> unstacked;
	recipientBonusOverlay::Groups groups; bool veteran = false;
	const recipientBonusOverlay::Layer layer({added, updated, removed, stacked, unstacked, groups, veteran});
	const std::array layers{layer};
	EXPECT_THROW(captureNativeOverlayModifiers(*graph, graph->root(), layers), std::invalid_argument);
}
}
