/*
 * NewHorizonsProspectorAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/CSkillHandler.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/Calendar.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/MiscObjects.h"
#include "lib/mapping/CMap.h"
#include "lib/modding/CModHandler.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES = "new-horizons:estates";
constexpr auto PROSPECTOR = "new-horizons:estates.prospector";

class NewHorizonsProspectorAITest : public NullkillerTest
{
protected:
	bool savedRegistry = true;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
	CGMine * common = nullptr;
	CGMine * rare = nullptr;
	CGMine * gold = nullptr;
	CGMine * neutral = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			savedRegistry ? JsonNode(JsonPath::builtin("config/newHorizonsPerks")) : JsonNode());
	}
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER)
			.heroGarrison({{CreatureID(27), 10}})
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER)
			.mine({8, 5, 0}, MapObjectSubID(0), PLAYER)
			.mine({12, 5, 0}, MapObjectSubID(1), PLAYER)
			.mine({8, 9, 0}, MapObjectSubID(6), PLAYER)
			.mine({18, 18, 0}, MapObjectSubID(0), PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));
		revealMap(PLAYER);
		hero = findHeroAt({4, 5, 0});
		otherHero = findHeroAt({4, 20, 0});
		common = dynamic_cast<CGMine *>(findObjectAt({8, 5, 0}));
		rare = dynamic_cast<CGMine *>(findObjectAt({12, 5, 0}));
		gold = dynamic_cast<CGMine *>(findObjectAt({8, 9, 0}));
		neutral = dynamic_cast<CGMine *>(findObjectAt({18, 18, 0}));
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(otherHero, nullptr);
		ASSERT_NE(common, nullptr);
		ASSERT_NE(rare, nullptr);
		ASSERT_NE(gold, nullptr);
		ASSERT_NE(neutral, nullptr);
		ASSERT_EQ(common->prospectorQuantity(), 2);
		ASSERT_EQ(rare->prospectorQuantity(), 1);
		ASSERT_EQ(gold->prospectorQuantity(), 0);
		if(selected)
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(ESTATES)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({ESTATES, PROSPECTOR});
			ASSERT_TRUE(hero->hasActivePerk(ESTATES, PROSPECTOR));
		}
		gateway = makeGateway(PLAYER);
	}
	void markUsed()
	{
		const auto calendar = gateway->cc->getCalendar();
		const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		handler.setObjPropertyValue(hero->id, ObjProperty::NEW_HORIZONS_PROSPECTOR_LAST_WEEK, week);
		ASSERT_TRUE(hero->hasUsedNewHorizonsProspector(week));
	}
	std::vector<const CGObjectInstance *> candidates()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update();
		ai->armyManager->update();
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {common->id, rare->id, gold->id};
		ai->memory->alreadyVisited = {common->id, rare->id, gold->id};
		ai->objectClusterizer->reset();
		ai->objectClusterizer->clusterize();
		return ai->objectClusterizer->getNearbyObjects();
	}
};
}

TEST_F(NewHorizonsProspectorAITest, PlannerAdmitsVisitedOwnedRewardMineButNotGold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, rare));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, gold));
	EXPECT_FALSE(NK2AI::isWeeklyRevisitable(PLAYER, common));
	EXPECT_EQ(reward.getGoldReward(common, hero), 200);
	EXPECT_EQ(reward.getGoldReward(rare, hero), 100);
	EXPECT_EQ(reward.getGoldReward(gold, hero), 0);
	const auto result = candidates();
	EXPECT_NE(std::ranges::find(result, common), result.end());
	EXPECT_EQ(std::ranges::find(result, gold), result.end());
}

TEST_F(NewHorizonsProspectorAITest, UsedReceiptRemovesOwnedMineRewardAndPlannerCandidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(markUsed());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_EQ(reward.getGoldReward(common, hero), 0);
	EXPECT_FLOAT_EQ(reward.getStrategicalValue(common, hero), 0.0f);
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsProspectorAITest, UnselectedHeroDoesNotAcquireOwnedMineTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_EQ(reward.getGoldReward(common, hero), 0);
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsProspectorAITest, MissingSavedRegistryDoesNotUseInstalledPerk)
{
	savedRegistry = false;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * ai = gateway->nullkiller.get();
	EXPECT_FALSE(hero->hasActivePerk(ESTATES, PROSPECTOR));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsProspectorAITest, ReceiptIsPerHeroAndEnemyCaptureValueStaysOrdinary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(ESTATES));
	otherHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	otherHero->applyPerkSelection({ESTATES, PROSPECTOR});
	ASSERT_TRUE(otherHero->hasActivePerk(ESTATES, PROSPECTOR));
	ASSERT_NO_FATAL_FAILURE(markUsed());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_TRUE(NK2AI::shouldVisit(ai, otherHero, common));
	EXPECT_EQ(reward.getGoldReward(common, otherHero), 200);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, neutral));
	EXPECT_EQ(reward.getGoldReward(neutral, hero), 375);
}
