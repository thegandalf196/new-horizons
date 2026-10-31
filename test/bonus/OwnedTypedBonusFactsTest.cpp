/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Input = OwnedRecipientBonusGraph::NodeInput;
using Facts = OwnedBonusEvaluationFacts;
struct WithoutLibrary
{
	GameLibrary * original = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = original; }
};
Input subject()
{
	Input input;
	input.facts.nodeType = BonusNodeType::STACK_BATTLE;
	input.facts.stackCount = 10;
	input.facts.stableStackLevel = 5;
	input.facts.creatureContext.emplace();
	input.facts.creatureContext->creature = Facts::CreatureContext::Creature{CreatureID(2), 2, EAlignment::EVIL};
	input.facts.creatureContext->faction = FactionID(7);
	input.facts.creatureContext->familyUpgrades[CreatureID(1)] = true;
	input.facts.creatureContext->familyUpgrades[CreatureID(3)] = false;
	input.facts.acceptance.emplace();
	input.facts.acceptance->state.hasForm = true;
	input.facts.acceptance->state.currentForm = CreatureID(2);
	input.facts.acceptance->state.originalForm = CreatureID(1);
	return input;
}
std::shared_ptr<Bonus> value(int amount = 5)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, amount, BonusSourceID());
}
std::shared_ptr<CCreatureTypeLimiter> family(CreatureID id, bool upgrades)
{
	auto result = std::make_shared<CCreatureTypeLimiter>();
	result->creatureID = id;
	result->includeUpgrades = upgrades;
	return result;
}
CSelector all() { return CSelector([](const Bonus *) { return true; }); }
std::unique_ptr<OwnedRecipientBonusGraph> graph(Input input, const std::shared_ptr<Bonus> & bonus)
{
	input.local.push_back(bonus);
	return OwnedRecipientBonusGraph::fromOwnedInputs({std::move(input)}, 0);
}
}
TEST(OwnedTypedBonusFactsTest, CreatureFamilyTierAlignmentUseCopiedFactsWithoutLibrary)
{
	auto input = subject();
	auto bonus = value();
	bonus->limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{family(CreatureID(1), true),
		std::make_shared<CreatureLevelLimiter>(2, 3), std::make_shared<CreatureAlignmentLimiter>(EAlignment::EVIL)});
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		auto owned = graph(input, bonus);
		input.facts.creatureContext->familyUpgrades[CreatureID(1)] = false;
		WithoutLibrary absent;
		EXPECT_EQ(owned->query(all()).valOfBonuses(all()), 5);
		EXPECT_EQ(owned->query(all()).valOfBonuses(all()), 5);
		EXPECT_EQ(owned->evaluation(owned->root()).stackLevel(), 5); //Distinct from creature tier2.
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
TEST(OwnedTypedBonusFactsTest, ExactFamilySkipsMissingUpgradeTableButUncapturedRelationThrows)
{
	auto input = subject();
	input.facts.creatureContext->familyUpgrades.clear();
	auto exact = value();
	exact->limiter = family(CreatureID(2), true);
	auto same = graph(input, exact);
	auto related = value();
	related->limiter = family(CreatureID(1), true);
	auto missing = graph(input, related);
	WithoutLibrary absent;
	EXPECT_EQ(same->query(all()).size(), 1u);
	EXPECT_THROW(missing->query(all()), std::logic_error);
}
TEST(OwnedTypedBonusFactsTest, CapturedCreatureAbsenceIsNotAnUnboundDomain)
{
	auto input = subject();
	input.facts.creatureContext->creature.reset();
	auto bonus = value();
	bonus->limiter = family(CreatureID(2), false);
	auto absent = graph(input, bonus);
	input.facts.creatureContext.reset();
	auto unbound = graph(input, bonus);
	WithoutLibrary noLibrary;
	EXPECT_TRUE(absent->query(all()).empty());
	EXPECT_THROW(unbound->query(all()), std::logic_error);
}
TEST(OwnedTypedBonusFactsTest, FactionDefaultsUseCapturedSourceWhileExplicitFactionShortCircuits)
{
	auto input = subject();
	input.facts.creatureContext->sourceFactions[CreatureID(10)] = FactionID(7);
	auto bonus = value();
	bonus->source = BonusSource::CREATURE_ABILITY;
	bonus->sid = BonusSourceID(CreatureID(10));
	bonus->limiter = std::make_shared<FactionLimiter>(FactionID::DEFAULT);
	auto matching = graph(input, bonus);
	bonus->sid = BonusSourceID(CreatureID(11));
	auto missing = graph(input, bonus);
	bonus->limiter = std::make_shared<FactionLimiter>(FactionID(7));
	auto explicitFaction = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_EQ(matching->query(all()).size(), 1u);
	EXPECT_THROW(missing->query(all()), std::logic_error);
	EXPECT_EQ(explicitFaction->query(all()).size(), 1u);
}
TEST(OwnedTypedBonusFactsTest, RankKeepsStrictBoundsCommanderDiscardAndKnownAbsence)
{
	auto input = subject();
	input.facts.rankContext.emplace();
	input.facts.rankContext->instance = Facts::RankContext::Instance{false, 5};
	auto bonus = value();
	bonus->limiter = std::make_shared<RankRangeLimiter>(3, 7);
	auto accepted = graph(input, bonus);
	input.facts.rankContext->instance = Facts::RankContext::Instance{false, 3};
	auto lower = graph(input, bonus);
	input.facts.rankContext->instance = Facts::RankContext::Instance{true, 5};
	auto commander = graph(input, bonus);
	input.facts.rankContext->instance.reset();
	auto noInstance = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_EQ(accepted->query(all()).size(), 1u);
	EXPECT_TRUE(lower->query(all()).empty());
	EXPECT_TRUE(commander->query(all()).empty());
	EXPECT_TRUE(noInstance->query(all()).empty());
}
TEST(OwnedTypedBonusFactsTest, TerrainPreservesAdventureNoneThenQueryBoundAndLegacyPrecedence)
{
	auto input = subject();
	input.facts.terrainContext.emplace();
	auto & terrain = *input.facts.terrainContext;
	terrain.adventure = TerrainId::NONE;
	terrain.query = TerrainId(4);
	terrain.battle = TerrainId(5);
	terrain.legacyFallback = TerrainId(6);
	terrain.explicitNativeContext = true;
	auto bonus = value();
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId::NONE);
	auto adventureNone = graph(input, bonus);
	terrain.adventure.reset();
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId(4));
	auto query = graph(input, bonus);
	terrain.query = TerrainId::NONE;
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId(5));
	auto bound = graph(input, bonus);
	terrain.query.reset();
	terrain.explicitNativeContext = false;
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId(6));
	auto legacy = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_EQ(adventureNone->query(all()).size(), 1u);
	EXPECT_EQ(query->query(all()).size(), 1u);
	EXPECT_EQ(bound->query(all()).size(), 1u);
	EXPECT_EQ(legacy->query(all()).size(), 1u);
}
TEST(OwnedTypedBonusFactsTest, ExplicitAbsentBattleCannotFallBackToLegacyOrNativeObject)
{
	auto input = subject();
	input.facts.terrainContext.emplace();
	input.facts.terrainContext->explicitNativeContext = true;
	input.facts.terrainContext->legacyFallback = TerrainId(6);
	auto bonus = value();
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId(6));
	auto owned = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_TRUE(owned->evaluation(owned->root()).hasNativeContext()); //Copied precedence fact, not authority.
	EXPECT_THROW(owned->query(all()), std::logic_error);
	EXPECT_THROW(owned->evaluation(owned->root()).battleContext(), std::logic_error);
}
TEST(OwnedTypedBonusFactsTest, NativeTerrainUsesMembershipOrExistingDependencyFixedPoint)
{
	auto input = subject();
	input.facts.terrainContext.emplace();
	input.facts.terrainContext->adventure = TerrainId(4);
	input.facts.terrainContext->nativeMembership[TerrainId(4)] = true;
	auto bonus = value();
	bonus->limiter = std::make_shared<TerrainLimiter>();
	auto native = graph(input, bonus);
	input.facts.terrainContext->nativeMembership[TerrainId(4)] = false;
	auto foreign = graph(input, bonus);
	input.facts.terrainContext->nativeMembership.clear();
	auto missing = graph(input, bonus);
	input.facts.terrainContext->adventure.reset();
	auto flag = value(1);
	flag->type = BonusType::TERRAIN_NATIVE;
	input.local = {bonus, flag};
	auto dependency = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
	WithoutLibrary absent;
	EXPECT_EQ(native->query(all()).size(), 1u);
	EXPECT_TRUE(foreign->query(all()).empty());
	EXPECT_THROW(missing->query(all()), std::logic_error);
	EXPECT_EQ(dependency->query(all(), true).size(), 2u);
}
TEST(OwnedTypedBonusFactsTest, CopiedAcceptancePreservesSummonAndGhostRules)
{
	const SecondarySkill sylvan(SecondarySkill::decode("new-horizons:sylvanLuck"));
	ASSERT_TRUE(sylvan.hasValue()); // CURRENT clone encodes a real registered identifier before NoLibrary.
	auto input = subject();
	auto & acceptance = *input.facts.acceptance;
	acceptance.metadata.sylvanSkill = sylvan;
	acceptance.metadata.heroPresent = acceptance.metadata.wildChance = true;
	acceptance.metadata.slot = SlotID(0);
	acceptance.state.summoned = acceptance.state.natureSummoned = true;
	auto bonus = value();
	bonus->source = BonusSource::SECONDARY_SKILL;
	bonus->sid = BonusSourceID(sylvan);
	auto nature = graph(input, bonus);
	acceptance.state.natureSummoned = false;
	auto ordinarySummon = graph(input, bonus);
	acceptance.metadata.tacticsEnabled = true;
	acceptance.state.ghost = true;
	auto ghost = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_EQ(nature->query(all()).size(), 1u);
	EXPECT_TRUE(ordinarySummon->query(all()).empty());
	EXPECT_TRUE(ghost->query(all()).empty());
}
TEST(OwnedTypedBonusFactsTest, CopiedFormAcceptanceFiltersOnlyOriginalTypedCreatureSources)
{
	auto input = subject();
	auto bonus = value();
	bonus->source = BonusSource::CREATURE_ABILITY;
	bonus->sid = BonusSourceID(CreatureID(1));
	auto original = graph(input, bonus);
	bonus->sid = BonusSourceID(CreatureID(2));
	auto current = graph(input, bonus);
	bonus->source = BonusSource::HERO_SPECIAL;
	bonus->sid = BonusSourceID(HeroTypeID(1));
	auto otherTypedSource = graph(input, bonus);
	WithoutLibrary absent;
	EXPECT_TRUE(original->query(all()).empty());
	EXPECT_EQ(current->query(all()).size(), 1u);
	EXPECT_EQ(otherTypedSource->query(all()).size(), 1u);
}
TEST(OwnedTypedBonusFactsTest, MissingAcceptanceAndLiveBridgesFailBeforeAnyNativeFallback)
{
	auto input = subject();
	auto bonus = value();
	input.facts.acceptance.reset();
	auto missing = graph(input, bonus);
	input = subject();
	auto owned = graph(input, bonus);
	const auto context = owned->evaluation(owned->root());
	WithoutLibrary absent;
	EXPECT_THROW(missing->query(all()), std::logic_error);
	EXPECT_THROW(context.creature(), std::logic_error);
	EXPECT_THROW(context.stackInstance(), std::logic_error);
	EXPECT_THROW(context.battleUnit(), std::logic_error);
	EXPECT_THROW(context.adjacentUnits(), std::logic_error);
	EXPECT_EQ(context.faction(), FactionID(7));
}
TEST(OwnedTypedBonusFactsTest, InheritedLimiterUsesRecipientFactsWithoutRequiringSourceCreature)
{
	auto input = subject();
	input.parents = {1};
	Input source;
	source.facts.nodeType = BonusNodeType::UNKNOWN;
	auto bonus = value();
	bonus->limiter = family(CreatureID(1), true);
	source.local = {bonus};
	auto owned = OwnedRecipientBonusGraph::fromOwnedInputs({input, source}, 0);
	WithoutLibrary absent;
	EXPECT_EQ(owned->query(all()).valOfBonuses(all()), 5);
}
