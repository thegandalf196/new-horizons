/*
 * SummonQueryTerrainTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/ProjectedSummonHealthQuery.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include <stdexcept>

namespace
{
using Query = newHorizonsElementalRebirth::ProjectedSummonHealthQuery;

std::shared_ptr<Bonus> health(int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, amount, BonusSourceID());
}

class TerrainWithoutBattleUpdater final : public IUpdater
{
	TerrainId terrain;
public:
	explicit TerrainWithoutBattleUpdater(TerrainId terrain) : terrain(terrain) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(context.getNodeType() != BonusNodeType::STACK_BATTLE)
			return bonus;
		auto result = std::make_shared<Bonus>(*bonus);
		result->val = !context.battleContext() && !context.battleTerrain()
			&& context.queryTerrain() == terrain && context.stackCount() == 1 ? 31 : -1000;
		result->updater = nullptr;
		return result;
	}
};

class SummonQueryTerrainTest : public BattleTestFixture
{
protected:
	std::vector<std::unique_ptr<Query>> queries;
	std::unique_ptr<ProjectedBonusGraph> graph;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
	}
	void TearDown() override
	{
		graph.reset();
		queries.clear();
		BattleTestFixture::TearDown();
	}
	Query * query(UnitQueryTerrain terrain)
	{
		if(!graph)
			graph = std::make_unique<ProjectedBonusGraph>();
		queries.push_back(Query::create(*graph, attackerSideHero, CreatureID(0), PlayerColor(0),
			BattleSide::ATTACKER, terrain));
		return queries.back().get();
	}
	int32_t scalar(UnitQueryTerrain terrain)
	{
		return newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero, CreatureID(0),
			PlayerColor(0), BattleSide::ATTACKER, terrain);
	}
};

TEST_F(SummonQueryTerrainTest, InheritedBattleTerrainIsDefinedWithoutInventingUpdaterBattleMembership)
{
	startBattle();
	const auto captured = captureUnitQueryTerrain(attackerSideHero);
	ASSERT_EQ(captured.terrain, battle()->getTerrainType());
	const auto baseline = scalar(captured); // old source throws on unrelated native terrain bonuses
	auto bonus = health(1);
	bonus->updater = std::make_shared<TerrainWithoutBattleUpdater>(*captured.terrain);
	attackerSideHero->addNewBonus(bonus);
	const auto expected = baseline + 31;
	EXPECT_EQ(newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero, CreatureID(0),
		PlayerColor(0), BattleSide::ATTACKER), expected);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto * projected = query(captured);
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->maximum(), expected);
	EXPECT_EQ(projected->identity().getBattle(), nullptr);
	EXPECT_EQ(projected->identity().getQueryTerrain(), captured.terrain);
	const BonusEvaluationContext native(projected->identity());
	EXPECT_EQ(native.battleContext(), nullptr);
	EXPECT_EQ(native.battleTerrain(), std::nullopt);
	EXPECT_EQ(native.queryTerrain(), captured.terrain);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(SummonQueryTerrainTest, ExplicitMissingTerrainCannotBorrowEvenAnActuallyBoundSource)
{
	startBattle();
	ASSERT_NE(attackerSideHero->battle, nullptr);
	EXPECT_THROW(scalar({}), std::logic_error);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(query({}), std::logic_error);
	EXPECT_TRUE(queries.empty());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph->capture(*attackerSideHero), std::logic_error);
}

TEST_F(SummonQueryTerrainTest, AdventureTileIsNotAnUnboundHypotheticalsInventedTerrain)
{
	ASSERT_EQ(attackerSideHero->battle, nullptr);
	EXPECT_FALSE(captureUnitQueryTerrain(attackerSideHero).terrain);
	auto bonus = health(5);
	bonus->limiter = std::make_shared<TerrainLimiter>(attackerSideHero->getCurrentTerrain());
	attackerSideHero->addNewBonus(bonus);
	EXPECT_THROW(newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero, CreatureID(0),
		PlayerColor(0), BattleSide::ATTACKER), std::logic_error);
	graph = std::make_unique<ProjectedBonusGraph>();
	EXPECT_THROW(Query::create(*graph, attackerSideHero, CreatureID(0), PlayerColor(0), BattleSide::ATTACKER), std::logic_error);
}

TEST_F(SummonQueryTerrainTest, SequentialPrivateQueriesShareCapturedTerrainAndUseLatestArmyBonuses)
{
	startBattle();
	const auto captured = captureUnitQueryTerrain(attackerSideHero);
	const auto * first = query(captured);
	ASSERT_NE(first, nullptr);
	const auto previous = first->maximum();
	auto bonus = health(5);
	bonus->limiter = std::make_shared<TerrainLimiter>(*captured.terrain);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph->addExported(*attackerSideHero, bonus);
	const auto * second = query(captured);
	ASSERT_NE(second, nullptr);
	EXPECT_NE(&first->identity(), &second->identity());
	EXPECT_EQ(second->maximum(), previous + 5);
	EXPECT_EQ(first->maximum(), previous);
	EXPECT_EQ(second->identity().getQueryTerrain(), first->identity().getQueryTerrain());
	EXPECT_EQ(second->identity().getBattle(), nullptr);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(SummonQueryTerrainTest, NativeProviderPriorityStillPrecedesStrictTerrainResolution)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 1);
	CStack original(&descriptor, PlayerColor(0), -1, BattleSide::ATTACKER,
		SlotID::SUMMONED_SLOT_PLACEHOLDER, true, {});
	BonusList accepted, pending;
	auto provider = health(1);
	provider->type = BonusType::TERRAIN_NATIVE;
	pending.push_back(provider);
	const BonusEvaluationContext context(original);
	const auto input = *health(1);
	EXPECT_EQ(TerrainLimiter().limit({input, original, accepted, pending, context}), ILimiter::EDecision::NOT_SURE);
	accepted.push_back(provider);
	EXPECT_EQ(TerrainLimiter().limit({input, original, accepted, pending, context}), ILimiter::EDecision::ACCEPT);
	EXPECT_THROW(TerrainLimiter(TerrainId(ETerrainId::GRASS)).limit({input, original, accepted, pending, context}), std::logic_error);
}

TEST_F(SummonQueryTerrainTest, UnavailableBoundTerrainStillCapturesExplicitAbsence)
{
	startBattle();
	battle()->terrainType = TerrainId(TerrainId::NONE); // fixture metadata, BEFORE any private capture
	EXPECT_FALSE(captureUnitQueryTerrain(attackerSideHero).terrain);
	EXPECT_THROW(newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero, CreatureID(0),
		PlayerColor(0), BattleSide::ATTACKER), std::logic_error);
}

TEST_F(SummonQueryTerrainTest, QueryMetadataCannotBeInstalledOnAnOrdinaryIdentity)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 1);
	EXPECT_THROW(CStack(&descriptor, PlayerColor(0), 999, BattleSide::ATTACKER,
		SlotID::SUMMONED_SLOT_PLACEHOLDER, false, UnitQueryTerrain{TerrainId(ETerrainId::GRASS)}), std::invalid_argument);
	CStack ordinary(&descriptor, PlayerColor(0), 999, BattleSide::ATTACKER);
	EXPECT_FALSE(ordinary.getQueryTerrain());
	EXPECT_EQ(ordinary.getBattle(), nullptr);
}

TEST_F(SummonQueryTerrainTest, ExplicitMissingContextDoesNotRecoverOwnedOriginalQueryMetadata)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 1);
	const TerrainId grass(ETerrainId::GRASS);
	CStack original(&descriptor, PlayerColor(0), -1, BattleSide::ATTACKER,
		SlotID::SUMMONED_SLOT_PLACEHOLDER, true, UnitQueryTerrain{grass});
	battle::CUnitStateDetached state(&original, &original);
	battle::UnitStateValueFields::copy(state, original);
	ProjectedBonusRoster roster({&state});
	const BonusEvaluationContext absent(original, state, roster, {nullptr, nullptr});
	EXPECT_EQ(original.getQueryTerrain(), grass);
	EXPECT_EQ(absent.queryTerrain(), std::nullopt);
	const auto input = *health(1);
	BonusList accepted, pending;
	EXPECT_THROW(TerrainLimiter(grass).limit({input, original, accepted, pending, absent}), std::logic_error);
}
}
