/*
 * TerrainAuthorityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include <stdexcept>

namespace
{
using Decision = ILimiter::EDecision;

class TerrainProjection
{
	ProjectedBonusGraph graph;
public:
	battle::CUnitStateDetached state;
	ProjectedBonusRoster roster;

	explicit TerrainProjection(const CStack & original)
		: state(&original, &graph.view(original)), roster({&state})
	{
		battle::UnitStateValueFields::copy(state, original); // unbound snapshot, no hook
		state.bindStateEnvironment(&original);
		graph.bind(original, state, roster);
	}

	TConstBonusListPtr query(const CStack & original, const CSelector & selector)
	{
		return graph.query(original, selector, true);
	}
};

class TerrainAuthorityTest : public BattleTestFixture
{
protected:
	BonusList accepted, pending;
	Bonus input{BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame(true);
		startBattle();
	}

	Decision decision(const ILimiter & limiter, const BonusEvaluationContext & evaluation)
	{
		return limiter.limit({input, evaluation.originalNode(), accepted, pending, evaluation});
	}

	void expectExplicit(const BonusEvaluationContext & evaluation, TerrainId equal, TerrainId unequal)
	{
		ASSERT_NE(equal, unequal);
		EXPECT_EQ(decision(TerrainLimiter(equal), evaluation), Decision::ACCEPT);
		EXPECT_EQ(decision(TerrainLimiter(unequal), evaluation), Decision::DISCARD);
	}

	void expectProviderPriority(const BonusEvaluationContext & evaluation)
	{
		auto provider = std::make_shared<Bonus>(input);
		provider->type = BonusType::TERRAIN_NATIVE;
		accepted.clear();
		pending.clear();
		pending.push_back(provider);
		EXPECT_EQ(decision(TerrainLimiter(), evaluation), Decision::NOT_SURE);
		accepted.push_back(provider);
		EXPECT_EQ(decision(TerrainLimiter(), evaluation), Decision::ACCEPT);
		pending.clear();
		EXPECT_EQ(decision(TerrainLimiter(), evaluation), Decision::ACCEPT);
		accepted.clear();
	}

	void expectNativeSource(const BonusEvaluationContext & evaluation, TerrainId terrain)
	{
		ASSERT_TRUE(evaluation.faction().has_value());
		const auto * faction = evaluation.faction()->toEntity(LIBRARY);
		EXPECT_EQ(decision(TerrainLimiter(), evaluation),
			faction->isNativeTerrain(terrain) ? Decision::ACCEPT : Decision::DISCARD);
	}

	void expectMissingTerrain(const ILimiter & limiter, const BonusEvaluationContext & evaluation)
	{
		try
		{
			(void)decision(limiter, evaluation);
			FAIL() << "Expected missing bound terrain context";
		}
		catch(const std::logic_error & error)
		{
			EXPECT_STREQ(error.what(), "Terrain limiter requires an adventure or bound battle terrain");
		}
	}
};

// The game, battle, army and health binding are maintained fixtures. No writes to
// CStack's private battle pointer, no localInit during projected evaluation.
TEST_F(TerrainAuthorityTest, BoundBaseNullUsesCapturedTerrainLiveAndProjectedIncludingNone)
{
	auto * stack = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 8);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->base, nullptr);
	ASSERT_EQ(stack->getBattle(), battle());
	TerrainProjection projection(*stack);
	auto & projected = projection.state;
	const BonusEvaluationContext live(*stack), view(*stack, projected);
	const auto armyTerrain = attackerSideHero->getCurrentTerrain();
	for(const TerrainId terrain : {TerrainId(ETerrainId::SNOW), TerrainId(TerrainId::NONE)})
	{
		ASSERT_NE(armyTerrain, terrain);
		battle()->terrainType = terrain;
		ASSERT_EQ(stack->getCurrentTerrain(), terrain);
		expectExplicit(live, terrain, TerrainId(ETerrainId::GRASS));
		expectExplicit(view, terrain, TerrainId(ETerrainId::GRASS));
		expectNativeSource(live, terrain);
		expectNativeSource(view, terrain);
	}
	EXPECT_EQ(attackerSideHero->getCurrentTerrain(), armyTerrain);
}

TEST_F(TerrainAuthorityTest, NativeFactionChangesButBattleTerrainDoesNotFollowProjectedForm)
{
	auto * stack = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 8);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->base, nullptr);
	TerrainProjection projection(*stack);
	auto & projected = projection.state;
	projected.beginBattleForm(CreatureID(28), 3);
	const BonusEvaluationContext live(*stack), view(*stack, projected);
	ASSERT_NE(live.faction(), view.faction());
	const auto * originalFaction = live.faction()->toEntity(LIBRARY);
	const auto * formFaction = view.faction()->toEntity(LIBRARY);
	const TerrainId grass(ETerrainId::GRASS), sand(ETerrainId::SAND);
	ASSERT_TRUE(originalFaction->isNativeTerrain(grass));
	ASSERT_FALSE(originalFaction->isNativeTerrain(sand));
	ASSERT_FALSE(formFaction->isNativeTerrain(grass));
	ASSERT_TRUE(formFaction->isNativeTerrain(sand));
	for(const auto terrain : {grass, sand})
	{
		battle()->terrainType = terrain;
		EXPECT_EQ(decision(TerrainLimiter(), live), terrain == grass ? Decision::ACCEPT : Decision::DISCARD);
		EXPECT_EQ(decision(TerrainLimiter(), view), terrain == sand ? Decision::ACCEPT : Decision::DISCARD);
		expectProviderPriority(live);
		expectProviderPriority(view);
		expectExplicit(live, terrain, terrain == grass ? sand : grass);
		expectExplicit(view, terrain, terrain == grass ? sand : grass);
	}
	EXPECT_FALSE(stack->hasBattleForm());
	EXPECT_EQ(stack->unitType()->getId(), CreatureID(0));
}

TEST_F(TerrainAuthorityTest, ExplicitTerrainNeverUsesNativeProviderShortcut)
{
	auto * stack = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 8);
	ASSERT_NE(stack, nullptr);
	TerrainProjection projection(*stack);
	auto & projected = projection.state;
	projected.beginBattleForm(CreatureID(28), 3);
	battle()->terrainType = TerrainId::NONE;
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::TERRAIN_NATIVE;
	for(int mode = 0; mode < 3; ++mode)
	{
		accepted.clear();
		pending.clear();
		if(mode != 1) accepted.push_back(provider);
		if(mode != 0) pending.push_back(provider);
		expectExplicit(*stack, TerrainId::NONE, TerrainId(ETerrainId::SNOW));
		expectExplicit(BonusEvaluationContext(*stack, projected), TerrainId::NONE, TerrainId(ETerrainId::SNOW));
	}
}

TEST_F(TerrainAuthorityTest, ActualLiveAndProjectedCollectorsRetainOnlyCapturedTerrainEffect)
{
	auto * stack = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 8);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->base, nullptr);
	ASSERT_EQ(stack->getBattle(), battle());
	battle()->terrainType = TerrainId(ETerrainId::SNOW);
	auto snow = std::make_shared<Bonus>(input);
	auto none = std::make_shared<Bonus>(input);
	snow->limiter = std::make_shared<TerrainLimiter>(TerrainId(ETerrainId::SNOW));
	none->limiter = std::make_shared<TerrainLimiter>(TerrainId::NONE);
	stack->addNewBonus(none);
	stack->addNewBonus(snow);
	const CSelector markers([&](const Bonus * bonus) { return bonus == snow.get() || bonus == none.get(); });
	const auto live = stack->getUnstackedBonuses(markers);
	ASSERT_EQ(live->size(), 1);
	EXPECT_EQ(live->front(), snow);
	TerrainProjection projection(*stack);
	projection.state.beginBattleForm(CreatureID(28), 3);
	const auto projected = projection.query(*stack, markers);
	ASSERT_EQ(projected->size(), 1);
	EXPECT_EQ(projected->front(), snow);
	EXPECT_EQ(stack->getBattle(), battle());
	EXPECT_FALSE(stack->hasBattleForm());
	EXPECT_EQ(snow->limiter->limit({input, *stack, accepted, pending}), Decision::ACCEPT);
}

TEST_F(TerrainAuthorityTest, BasedBattleStackKeepsAdventureArmyTerrainAcrossProjection)
{
	CStack * stack = nullptr;
	for(auto & unit : battle()->stacks)
		if(unit->base && unit->unitSide() == BattleSide::ATTACKER)
			stack = unit.get();
	ASSERT_NE(stack, nullptr);
	const auto sourceTerrain = stack->base->getCurrentTerrain();
	battle()->terrainType = TerrainId(ETerrainId::SNOW);
	ASSERT_NE(sourceTerrain, battle()->getTerrainType());
	TerrainProjection projection(*stack);
	auto & projected = projection.state;
	projected.beginBattleForm(CreatureID(28), 3);
	expectExplicit(*stack, sourceTerrain, battle()->getTerrainType());
	expectExplicit(BonusEvaluationContext(*stack, projected), sourceTerrain, battle()->getTerrainType());
	expectNativeSource(*stack, sourceTerrain);
	expectNativeSource(BonusEvaluationContext(*stack, projected), sourceTerrain);
	expectProviderPriority(*stack);
	expectProviderPriority(BonusEvaluationContext(*stack, projected));
}

TEST_F(TerrainAuthorityTest, BasedStackWithoutArmyPreservesExplicitNoneEvenWhenBound)
{
	CStackInstance base(gameState().get(), CreatureID(0), 8);
	ASSERT_EQ(base.getArmy(), nullptr);
	ASSERT_EQ(base.getCurrentTerrain(), TerrainId::NONE);
	CStack stack(&base, PlayerColor(0), 1000, BattleSide::ATTACKER, SlotID(0));
	stack.localInit(battle());
	battle()->terrainType = TerrainId(ETerrainId::GRASS);
	ASSERT_EQ(stack.getBattle(), battle());
	TerrainProjection projection(stack);
	auto & projected = projection.state;
	projected.beginBattleForm(CreatureID(28), 3);
	expectExplicit(base, TerrainId::NONE, battle()->getTerrainType());
	expectExplicit(stack, TerrainId::NONE, battle()->getTerrainType());
	expectExplicit(BonusEvaluationContext(stack, projected), TerrainId::NONE, battle()->getTerrainType());
	expectNativeSource(base, TerrainId::NONE);
	expectNativeSource(stack, TerrainId::NONE);
	expectNativeSource(BonusEvaluationContext(stack, projected), TerrainId::NONE);
	expectProviderPriority(base);
	expectProviderPriority(stack);
	expectProviderPriority(BonusEvaluationContext(stack, projected));
}

TEST_F(TerrainAuthorityTest, AdventureStackAndActualHeroTownKeepTheirOwnTileSource)
{
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_FALSE(attackerSideHero->Slots().empty());
	const auto * adventure = attackerSideHero->Slots().begin()->second.get();
	ASSERT_NE(adventure, nullptr);
	map()->getTile(attackerSideHero->visitablePos()).terrainType = TerrainId(ETerrainId::DIRT);
	map()->getTile(town->visitablePos()).terrainType = TerrainId(ETerrainId::SAND);
	battle()->terrainType = TerrainId(ETerrainId::SNOW);
	ASSERT_EQ(adventure->getCurrentTerrain(), TerrainId(ETerrainId::DIRT));
	ASSERT_EQ(attackerSideHero->getCurrentTerrain(), TerrainId(ETerrainId::DIRT));
	ASSERT_EQ(town->getCurrentTerrain(), TerrainId(ETerrainId::SAND));
	expectExplicit(*adventure, TerrainId(ETerrainId::DIRT), battle()->getTerrainType());
	expectExplicit(*attackerSideHero, TerrainId(ETerrainId::DIRT), battle()->getTerrainType());
	expectExplicit(*town, TerrainId(ETerrainId::SAND), battle()->getTerrainType());
	expectNativeSource(*adventure, TerrainId(ETerrainId::DIRT));
	expectNativeSource(*attackerSideHero, TerrainId(ETerrainId::DIRT));
	expectNativeSource(*town, TerrainId(ETerrainId::SAND));
	expectProviderPriority(*adventure);
	expectProviderPriority(*attackerSideHero);
	expectProviderPriority(*town);
}

TEST_F(TerrainAuthorityTest, ActualHeroTownAndTheirArmyPreserveNoneForInvalidPosition)
{
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_FALSE(attackerSideHero->Slots().empty());
	const auto * adventure = attackerSideHero->Slots().begin()->second.get();
	const auto heroPos = attackerSideHero->anchorPos(), townPos = town->anchorPos();
	attackerSideHero->setAnchorPos(int3(-1, -1, -1));
	town->setAnchorPos(int3(-1, -1, -1));
	battle()->terrainType = TerrainId(ETerrainId::GRASS);
	expectExplicit(*attackerSideHero, TerrainId::NONE, battle()->getTerrainType());
	expectExplicit(*town, TerrainId::NONE, battle()->getTerrainType());
	expectExplicit(*adventure, TerrainId::NONE, battle()->getTerrainType());
	expectNativeSource(*attackerSideHero, TerrainId::NONE);
	expectNativeSource(*town, TerrainId::NONE);
	expectNativeSource(*adventure, TerrainId::NONE);
	bool checkedBasedStack = false;
	for(const auto & unit : battle()->stacks)
		if(unit->base == adventure)
		{
			checkedBasedStack = true;
			TerrainProjection projection(*unit);
			auto & projected = projection.state;
			projected.beginBattleForm(CreatureID(28), 3);
			expectExplicit(*unit, TerrainId::NONE, battle()->getTerrainType());
			expectExplicit(BonusEvaluationContext(*unit, projected), TerrainId::NONE, battle()->getTerrainType());
			expectNativeSource(*unit, TerrainId::NONE);
			expectNativeSource(BonusEvaluationContext(*unit, projected), TerrainId::NONE);
		}
	EXPECT_TRUE(checkedBasedStack);
	expectProviderPriority(*attackerSideHero);
	expectProviderPriority(*town);
	expectProviderPriority(*adventure);
	attackerSideHero->setAnchorPos(heroPos);
	town->setAnchorPos(townPos);
}

TEST_F(TerrainAuthorityTest, UnboundBaseNullRequiresContextOnlyWhenTerrainIsNeeded)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 8);
	CStack stack(&descriptor, PlayerColor(0), 1000, BattleSide::ATTACKER);
	ASSERT_EQ(stack.base, nullptr);
	ASSERT_EQ(stack.getBattle(), nullptr);
	battle::CUnitStateDetached projected(&stack, &stack);
	// No localInit on an unbound stack: its environment legitimately needs a battle.
	const BonusEvaluationContext live(stack), view(stack, projected);
	expectProviderPriority(live);
	expectProviderPriority(view);
	for(const auto * evaluation : {&live, &view})
	{
		expectMissingTerrain(TerrainLimiter(TerrainId::NONE), *evaluation);
		expectMissingTerrain(TerrainLimiter(TerrainId(ETerrainId::GRASS)), *evaluation);
		expectMissingTerrain(TerrainLimiter(), *evaluation);
		auto provider = std::make_shared<Bonus>(input);
		provider->type = BonusType::TERRAIN_NATIVE;
		pending.push_back(provider);
		expectMissingTerrain(TerrainLimiter(TerrainId::NONE), *evaluation);
		accepted.push_back(provider);
		expectMissingTerrain(TerrainLimiter(TerrainId::NONE), *evaluation);
		pending.clear();
		expectMissingTerrain(TerrainLimiter(TerrainId::NONE), *evaluation);
		accepted.clear();
	}
}

TEST_F(TerrainAuthorityTest, RealSummonedAdditionRemainsQueryableAfterNormalBinding)
{
	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId();
	info.count = 8;
	info.type = CreatureID(0);
	info.side = BattleSide::ATTACKER;
	info.position = BattleHex(leftHex);
	info.summoned = true;
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(packet.changedStacks.back().data);
	gameHandler->sendAndApply(packet);
	auto * stack = battle()->getStack(info.id);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->base, nullptr);
	ASSERT_EQ(stack->getBattle(), battle());
	ASSERT_TRUE(stack->summoned);
	ASSERT_EQ(stack->unitSlot(), SlotID::SUMMONED_SLOT_PLACEHOLDER);
	TerrainProjection projection(*stack);
	auto & projected = projection.state;
	projected.beginBattleForm(CreatureID(28), 3);
	const auto terrain = battle()->getTerrainType();
	const TerrainId unequal = terrain == TerrainId::NONE ? TerrainId(ETerrainId::SNOW) : TerrainId(TerrainId::NONE);
	expectExplicit(*stack, terrain, unequal);
	expectExplicit(BonusEvaluationContext(*stack, projected), terrain, unequal);
	expectProviderPriority(*stack);
	expectProviderPriority(BonusEvaluationContext(*stack, projected));
}

TEST_F(TerrainAuthorityTest, PlainTownVisitorOnlyExercisesDefinedProviderBranches)
{
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->townAndVis.getNodeType(), BonusNodeType::TOWN_AND_VISITOR);
	EXPECT_EQ(dynamic_cast<const INativeTerrainProvider *>(&town->townAndVis), nullptr);
	expectProviderPriority(town->townAndVis);
	// No native fallback oracle: this plain node has no terrain/faction authority.
	// Its complete allowed-query domain remains a separate unresolved gate.
}
}
