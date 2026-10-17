/*
 * ProjectedSummonHealthQueryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/ProjectedSummonHealthQuery.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/SummonHealthQuery.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Limiters.h"
#include <stdexcept>

namespace
{
using Query = newHorizonsElementalRebirth::ProjectedSummonHealthQuery;

std::shared_ptr<Bonus> summonHealth(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, value, BonusSourceID());
}

class HypotheticalIdentityUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(context.getNodeType() != BonusNodeType::STACK_BATTLE)
			return bonus;
		const auto & original = dynamic_cast<const CStack &>(context.originalNode());
		const auto & unit = dynamic_cast<const battle::CUnitState &>(*context.battleUnit());
		auto result = std::make_shared<Bonus>(*bonus);
		result->val = original.unitId() == uint32_t(-1) && context.stackCount() == 1
			&& original.isHypothetic() && unit.summoned && !unit.natureSummoned
			&& !context.battleContext() ? 31 : -1000;
		result->updater = nullptr;
		return result;
	}
};

class RejectSummonUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		if(context.getNodeType() == BonusNodeType::STACK_BATTLE)
			throw std::runtime_error("synthetic hypothetical query failure");
		return bonus;
	}
};

class ProjectedSummonHealthQueryTest : public BattleTestFixture
{
protected:
	std::vector<std::unique_ptr<Query>> queries;
	std::unique_ptr<ProjectedBonusGraph> graph;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame(true);
		startBattle();
		graph = std::make_unique<ProjectedBonusGraph>();
	}
	void TearDown() override
	{
		graph.reset();
		queries.clear(); // retained identities outlive graph keys/journal
		BattleTestFixture::TearDown();
	}
	Query * prepare(const CArmedInstance * army = nullptr)
	{
		queries.push_back(Query::create(*graph, army, CreatureID(0), PlayerColor(0), BattleSide::ATTACKER));
		return queries.back().get();
	}
};

TEST_F(ProjectedSummonHealthQueryTest, ScalarOracleAndProjectedCountOneIdentityAgree)
{
	auto bonus = summonHealth(1);
	bonus->updater = std::make_shared<HypotheticalIdentityUpdater>();
	attackerSideHero->addNewBonus(bonus);
	const auto scalar = newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero,
		CreatureID(0), PlayerColor(0), BattleSide::ATTACKER);
	const auto before = PreparedBonusGraphStorage::currentGeneration();
	const auto * query = prepare(attackerSideHero);
	ASSERT_NE(query, nullptr);
	EXPECT_EQ(query->maximum(), scalar);
	EXPECT_GT(query->maximum(), 31); // native creature HP plus distinguishing typed updater
	EXPECT_EQ(query->identity().unitId(), uint32_t(-1));
	EXPECT_EQ(query->identity().getCount(), 1);
	EXPECT_EQ(query->identity().getBattle(), nullptr);
	EXPECT_TRUE(query->identity().getParentNodes().empty());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), before);
}

TEST_F(ProjectedSummonHealthQueryTest, ArmyThenCreatureAttachAndFrontDetachRecordFourAdvances)
{
	const auto * query = prepare(attackerSideHero);
	ASSERT_NE(query, nullptr);
	const auto & events = graph->invalidationEvents();
	ASSERT_EQ(events.size(), 8);
	for(size_t i = 0; i < events.size(); i += 2)
	{
		EXPECT_TRUE(events[i].advance);
		EXPECT_TRUE(events[i].targets.empty());
		EXPECT_FALSE(events[i + 1].advance);
		EXPECT_EQ(events[i + 1].targets, (TCNodesVector{&query->identity()}));
	}
	EXPECT_TRUE(graph->parents(query->identity()).empty());
	EXPECT_TRUE(liveBonusGraph().children(query->identity()).empty());
}

TEST_F(ProjectedSummonHealthQueryTest, NoArmyRetainsTwoAdvancesAndSameScalarFormula)
{
	const auto scalar = newHorizonsElementalRebirth::effectiveSummonMaxHP(nullptr,
		CreatureID(0), PlayerColor(0), BattleSide::ATTACKER);
	const auto * query = prepare();
	ASSERT_NE(query, nullptr);
	EXPECT_EQ(query->maximum(), scalar);
	const auto & events = graph->invalidationEvents();
	ASSERT_EQ(events.size(), 4);
	EXPECT_TRUE(events[0].advance && events[2].advance);
	EXPECT_FALSE(events[1].advance || events[3].advance);
}

TEST_F(ProjectedSummonHealthQueryTest, TemporaryIdentitiesNeverEnterBattlefieldRosterOrLookup)
{
	const auto initial = battle()->getUnitsIf([](const battle::Unit *) { return true; });
	ProjectedBonusRoster battlefield(std::vector<const battle::Unit *>(initial.begin(), initial.end()));
	const auto * first = prepare(attackerSideHero);
	const auto * second = prepare(attackerSideHero);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_NE(&first->identity(), &second->identity());
	EXPECT_EQ(first->identity().unitId(), second->identity().unitId());
	EXPECT_EQ(battlefield.orderedUnits().size(), initial.size());
	EXPECT_EQ(battlefield.firstById(uint32_t(-1)), nullptr);
	EXPECT_FALSE(battlefield.contains(first->identity()));
	EXPECT_EQ(battle()->getUnitsIf([](const battle::Unit *) { return true; }), initial);
}

TEST_F(ProjectedSummonHealthQueryTest, LaterQueryUsesSequentialPrivateArmyBonusesNotLiveFallback)
{
	const auto * first = prepare(attackerSideHero);
	ASSERT_NE(first, nullptr);
	const auto original = first->maximum();
	const auto version = PreparedBonusGraphStorage::currentGeneration();
	graph->addExported(*attackerSideHero, summonHealth(5));
	const auto * second = prepare(attackerSideHero);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(second->maximum(), original + 5);
	EXPECT_EQ(first->maximum(), original);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), version);
}

TEST_F(ProjectedSummonHealthQueryTest, InvalidCreatureOrSideHasNoIdentityOrEventPrefix)
{
	EXPECT_EQ(Query::create(*graph, nullptr, CreatureID(-1), PlayerColor(0), BattleSide::ATTACKER), nullptr);
	EXPECT_EQ(Query::create(*graph, nullptr, CreatureID(0), PlayerColor(0), BattleSide::NONE), nullptr);
	EXPECT_TRUE(graph->capturedNodes().empty());
	EXPECT_TRUE(graph->invalidationEvents().empty());
}

TEST_F(ProjectedSummonHealthQueryTest, QueryFailurePoisonsBeforeEphemeralIdentityCanDangle)
{
	auto bonus = summonHealth(1);
	bonus->updater = std::make_shared<RejectSummonUpdater>();
	attackerSideHero->addNewBonus(bonus);
	const auto version = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(prepare(attackerSideHero), std::runtime_error);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), version);
	EXPECT_THROW(graph->capture(*attackerSideHero), std::logic_error);
	EXPECT_TRUE(queries.empty());
}

TEST_F(ProjectedSummonHealthQueryTest, ExplicitTerrainRequiresSameAbsentBattleProvenanceAsScalar)
{
	auto bonus = summonHealth(5);
	bonus->limiter = std::make_shared<TerrainLimiter>(TerrainId(ETerrainId::GRASS));
	attackerSideHero->addNewBonus(bonus);
	EXPECT_THROW(newHorizonsElementalRebirth::effectiveSummonMaxHP(attackerSideHero,
		CreatureID(0), PlayerColor(0), BattleSide::ATTACKER, UnitQueryTerrain{}), std::logic_error);
	const auto version = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(Query::create(*graph, attackerSideHero, CreatureID(0), PlayerColor(0),
		BattleSide::ATTACKER, UnitQueryTerrain{}), std::logic_error);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), version);
}

TEST_F(ProjectedSummonHealthQueryTest, RetiredGraphThenOwnedTemporariesHaveNoNativeTeardownEvents)
{
	ASSERT_NE(prepare(attackerSideHero), nullptr);
	graph.reset(); // graph/journal no longer borrow either retained original
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	queries.clear();
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
}
