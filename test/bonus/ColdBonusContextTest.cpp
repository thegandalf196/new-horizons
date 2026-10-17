/*
 * ColdBonusContextTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/battle/BattleBonusAcceptance.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/entities/faction/CFaction.h"
#include <stdexcept>

namespace
{
class ColdBonusContextTest : public BattleTestFixture
{
protected:
	std::unique_ptr<CStack> original;
	std::unique_ptr<battle::CUnitStateDetached> state;
	std::unique_ptr<ProjectedBonusRoster> roster;
	std::vector<std::unique_ptr<CBonusSystemNode>> extraNodes;
	std::unique_ptr<ProjectedBonusGraph> graph;
	BonusList accepted, pending;
	Bonus input{BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame(true);
		startBattle();
		CStackBasicDescriptor descriptor(CreatureID(0), 4);
		original = std::make_unique<CStack>(&descriptor, battle()->getSidePlayer(BattleSide::ATTACKER), 1000,
			BattleSide::ATTACKER, SlotID::SUMMONED_SLOT_PLACEHOLDER);
		original->summoned = original->natureSummoned = true;
		graph = std::make_unique<ProjectedBonusGraph>();
		state = std::make_unique<battle::CUnitStateDetached>(original.get(), &graph->view(*original));
		battle::UnitStateValueFields::copy(*state, *original);
		roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{state.get()},
			ProjectedBonusRoster::Identity::OrderedOccurrences);
	}

	void TearDown() override
	{
		graph.reset(); // all graph/view keys die BEFORE private or game identities
		roster.reset();
		state.reset();
		original.reset();
		extraNodes.clear();
		BattleTestFixture::TearDown();
	}

	void bind(const BattleInfo * battle)
	{
		graph->bindColdOccurrence(*original, *state, *roster, battle);
	}

	ILimiter::EDecision decision(const ILimiter & limiter, const BonusEvaluationContext & context)
	{
		return limiter.limit({input, context.originalNode(), accepted, pending, context});
	}

	Bonus skillBonus()
	{
		return Bonus(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::SECONDARY_SKILL, 2,
			BonusSourceID(SecondarySkill(SecondarySkill::decode("new-horizons:sylvanLuck"))));
	}

	void selectWildChance()
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		// Strict progression requires a legal Basic prefix before Advanced Wild Chance.
		attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"));
		attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.wildChance"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.wildChance"));
	}
};

TEST_F(ColdBonusContextTest, ExplicitBoundBattleProvidesColdTerrainWithoutNativeMutation)
{
	bind(battle());
	const auto version = PreparedBonusGraphStorage::currentGeneration();
	const auto context = graph->evaluation(*original);
	EXPECT_TRUE(context.hasNativeContext());
	EXPECT_EQ(context.battleContext(), battle());
	EXPECT_EQ(context.battleTerrain(), battle()->getTerrainType());
	EXPECT_EQ(decision(TerrainLimiter(battle()->getTerrainType()), context), ILimiter::EDecision::ACCEPT);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), version);
	EXPECT_EQ(original->getBattle(), nullptr);
	EXPECT_TRUE(original->getParentNodes().empty());
}

TEST_F(ColdBonusContextTest, ExplicitAbsentBattleNeverFallsBackToBoundNativeOriginal)
{
	auto * live = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
	ASSERT_NE(live, nullptr);
	battle::CUnitStateDetached view(live, live);
	battle::UnitStateValueFields::copy(view, *live);
	ProjectedBonusRoster domain({&view});
	const BonusEvaluationContext ordinary(*live, view, domain);
	const BonusEvaluationContext absent(*live, view, domain, {nullptr, nullptr});
	EXPECT_FALSE(ordinary.hasNativeContext());
	EXPECT_EQ(ordinary.battleContext(), live->getBattle());
	EXPECT_EQ(decision(TerrainLimiter(battle()->getTerrainType()), ordinary), ILimiter::EDecision::ACCEPT);
	EXPECT_EQ(absent.battleContext(), nullptr);
	EXPECT_THROW(decision(TerrainLimiter(battle()->getTerrainType()), absent), std::logic_error);
}

TEST_F(ColdBonusContextTest, NativeTerrainProviderPrecedenceStaysLazyWhenBattleIsExplicitlyAbsent)
{
	bind(nullptr);
	const auto context = graph->evaluation(*original);
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::TERRAIN_NATIVE;
	pending.push_back(provider);
	EXPECT_EQ(decision(TerrainLimiter(), context), ILimiter::EDecision::NOT_SURE);
	accepted.push_back(provider);
	EXPECT_EQ(decision(TerrainLimiter(), context), ILimiter::EDecision::ACCEPT);
	EXPECT_THROW(decision(TerrainLimiter(TerrainId(ETerrainId::GRASS)), context), std::logic_error);
}

TEST_F(ColdBonusContextTest, ColdHeroUsesProjectedParentRatherThanEmptyNativeGraph)
{
	ASSERT_NO_FATAL_FAILURE(selectWildChance()); // before journal capture: live fixture setup only
	bind(battle());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph->attachToSource(*original, *attackerSideHero);
	const auto bonus = skillBonus();
	EXPECT_EQ(original->getMyHero(), nullptr);
	EXPECT_TRUE(graph->evaluation(*original).acceptsBonus(bonus));
	EXPECT_FALSE(BonusEvaluationContext(*original, *state, *roster).acceptsBonus(bonus));
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ColdBonusContextTest, ProjectedHeroFollowsExactFirstParentAndDetachOrder)
{
	ASSERT_NO_FATAL_FAILURE(selectWildChance());
	bind(battle());
	graph->attachToSource(*original, *attackerSideHero);
	graph->attachToSource(*original, *defenderSideHero);
	const auto bonus = skillBonus();
	EXPECT_TRUE(graph->evaluation(*original).acceptsBonus(bonus));
	graph->detachFromSource(*original, *attackerSideHero);
	EXPECT_FALSE(graph->evaluation(*original).acceptsBonus(bonus));
	graph->attachToSource(*original, *attackerSideHero);
	EXPECT_FALSE(graph->evaluation(*original).acceptsBonus(bonus));
	graph->detachFromSource(*original, *defenderSideHero);
	EXPECT_TRUE(graph->evaluation(*original).acceptsBonus(bonus));
}

TEST_F(ColdBonusContextTest, UntypedFirstHeroParentDoesNotSkipForwardToTypedHero)
{
	ASSERT_NO_FATAL_FAILURE(selectWildChance());
	bind(battle());
	extraNodes.push_back(std::make_unique<CBonusSystemNode>(BonusNodeType::HERO));
	graph->attachToSource(*original, *extraNodes.back());
	graph->attachToSource(*original, *attackerSideHero);
	EXPECT_FALSE(graph->evaluation(*original).acceptsBonus(skillBonus()));
}

TEST_F(ColdBonusContextTest, ExplicitNullHeroDoesNotFallBackAndEarlyAcceptanceIsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(selectWildChance());
	auto * live = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
	ASSERT_NE(live, nullptr);
	battle::CUnitStateDetached view(live, live);
	battle::UnitStateValueFields::copy(view, *live);
	view.summoned = view.natureSummoned = true;
	ProjectedBonusRoster domain({&view});
	const BonusEvaluationContext unspecified(*live, view, domain);
	const BonusEvaluationContext explicitNull(*live, view, domain, {nullptr, battle()});
	EXPECT_TRUE(unspecified.acceptsBonus(skillBonus()));
	EXPECT_FALSE(explicitNull.acceptsBonus(skillBonus()));
	EXPECT_EQ(unspecified.acceptsBonus(input), explicitNull.acceptsBonus(input));
}

TEST_F(ColdBonusContextTest, ColdBindingRejectsAlreadyAttachedNativeIdentity)
{
	original->attachToSource(*attackerSideHero); // deliberate fixture misuse, before private journal
	EXPECT_THROW(bind(battle()), std::invalid_argument);
	EXPECT_FALSE(graph->evaluation(*original).hasNativeContext());
	original->detachFromAll();
}

TEST_F(ColdBonusContextTest, ExplicitNoBattleQueryCannotInventSingletonAdjacency)
{
	bind(nullptr);
	EXPECT_THROW(graph->evaluation(*original).adjacentUnits(), std::logic_error);
	EXPECT_TRUE(original->getParentNodes().empty());
}
}
