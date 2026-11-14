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
#include "lib/CPlayerState.h"
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
constexpr auto PROSPERITY = "new-horizons:estates.prosperity";

class NewHorizonsProspectorAITest : public NullkillerTest
{
protected:
	bool savedRegistry = true;
	bool includeForeign = false;
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
		if(!savedRegistry)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
	}
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1}})
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER)
			.mine({8, 5, 0}, MapObjectSubID(0), PLAYER)
			.mine({12, 5, 0}, MapObjectSubID(1), PLAYER)
			.mine({8, 9, 0}, MapObjectSubID(6), PLAYER)
			.mine({18, 18, 0}, MapObjectSubID(0), PlayerColor::NEUTRAL);
		if(includeForeign) builder.playerActive(PlayerColor(1)).hero({30, 30, 0}, HeroTypeID(2), PlayerColor(1));
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
		if(selected)
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(ESTATES)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({ESTATES, PROSPERITY});
			ASSERT_TRUE(hero->hasActivePerk(ESTATES, PROSPERITY));
		}
		gateway = makeGateway(PLAYER);
	}
	void markLandSurveyorUsed()
	{
		const auto calendar = gateway->cc->getCalendar();
		const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		handler.setObjPropertyValue(hero->id, ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK, week);
		ASSERT_TRUE(hero->hasUsedNewHorizonsLandSurveyor(week));
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

TEST_F(NewHorizonsProspectorAITest, ProsperityIncomeDoesNotCreateVisitedOwnedMineTargets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, rare));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, gold));
	EXPECT_FALSE(NK2AI::isWeeklyRevisitable(PLAYER, common));
	EXPECT_EQ(hero->dailyIncome()[EGameResID::WOOD], 1);
	EXPECT_EQ(hero->dailyIncome()[EGameResID::ORE], 1);
	EXPECT_EQ(reward.getGoldReward(common, hero), 0);
	EXPECT_EQ(reward.getGoldReward(rare, hero), 0);
	EXPECT_EQ(reward.getGoldReward(gold, hero), 0);
	const auto result = candidates();
	EXPECT_EQ(std::ranges::find(result, common), result.end());
	EXPECT_EQ(std::ranges::find(result, gold), result.end());
}

TEST_F(NewHorizonsProspectorAITest, CaptureReceiptDoesNotTurnOwnedMinesIntoRevisitTargets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(markLandSurveyorUsed());
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

TEST_F(NewHorizonsProspectorAITest, MissingCapturedRegistryDoesNotUseInstalledPerk)
{
	savedRegistry = false;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * ai = gateway->nullkiller.get();
	EXPECT_FALSE(hero->hasActivePerk(ESTATES, PROSPERITY));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsProspectorAITest, IndependentHoldersKeepEnemyCaptureValueOrdinary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(ESTATES));
	otherHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	otherHero->applyPerkSelection({ESTATES, PROSPERITY});
	ASSERT_TRUE(otherHero->hasActivePerk(ESTATES, PROSPERITY));
	ASSERT_NO_FATAL_FAILURE(markLandSurveyorUsed());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, common));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, otherHero, common));
	EXPECT_EQ(reward.getGoldReward(common, otherHero), 0);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, neutral));
	EXPECT_EQ(reward.getGoldReward(neutral, hero), 375);
}

TEST_F(NewHorizonsProspectorAITest, DeliveredOwnedMineGuardsEnableRevisitAndSharedArmyRewardWithoutIncomeReward)
{
 ASSERT_NO_FATAL_FAILURE(prepare());
 const CreatureID rogue(CreatureID::decode("core:rogue"));
 common->clearSlots(); ASSERT_TRUE(common->setCreature(SlotID(0), rogue, 5));
 ASSERT_TRUE(gateway->nullkiller->settings->isGarrisonTroopsUsageAllowed());
 const auto before = gameState()->saveToMemory();
 const auto expected = gateway->nullkiller->armyManager->howManyReinforcementsCanGet(hero, hero, common, TerrainId::NONE);
 ASSERT_GT(expected, 0u);
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), expected);
 EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, common));
 NK2AI::RewardEvaluator rewards(gateway->nullkiller.get());
 EXPECT_EQ(rewards.getArmyReward(common, hero, hero, true), expected);
 EXPECT_EQ(rewards.getGoldReward(common, hero), 0);
 EXPECT_FLOAT_EQ(rewards.getStrategicalValue(common, hero), 0.0f);
 EXPECT_EQ(gameState()->saveToMemory(), before);
 const auto planned = candidates();
 EXPECT_NE(std::ranges::find(planned, common), planned.end());
}

TEST_F(NewHorizonsProspectorAITest, DepletedMineHasNoRepeatedCollectionCandidate)
{
 ASSERT_NO_FATAL_FAILURE(prepare());
 common->clearSlots(); ASSERT_TRUE(common->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:rogue")), 5));
 ASSERT_GT(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 common->clearSlots();
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, common));
 EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsProspectorAITest, HiddenAlliedAndForeignMineContextsCannotBorrowCollectionValue)
{
 includeForeign = true;
 ASSERT_NO_FATAL_FAILURE(prepare());
 common->clearSlots(); ASSERT_TRUE(common->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:rogue")), 5));
 auto * team = gameState()->getPlayerTeam(PLAYER);
 const auto pos = common->visitablePos(); team->fogOfWarMap[pos] = 0;
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 team->fogOfWarMap[pos] = 1;
 const auto * foreign = findHeroAt({30, 30, 0}); ASSERT_NE(foreign, nullptr);
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), foreign, common), 0u);
 GameHandlerTestServer server(gameState(), PLAYER); CGameHandler handler(server, gameState());
 handler.setOwner(common, PlayerColor(1));
 const auto ownedTeam = gameState()->players.at(PLAYER).team;
 gameState()->players.at(PlayerColor(1)).team = ownedTeam;
 gameState()->teams.at(ownedTeam).players.insert(PlayerColor(1));
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), foreign, common), 0u);
}

TEST_F(NewHorizonsProspectorAITest, CurrentV7SevenDistinctLinesRejectNewGuardsButMatchingOversizedLineRemainsLegal)
{
 ASSERT_NO_FATAL_FAILURE(prepare());
 const CreatureID rogue(CreatureID::decode("core:rogue"));
 ASSERT_FALSE(hero->getLeadershipSlotCapacity(rogue));
 hero->clearSlots(); ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:archDevil")), 100));
 int slot = 1;
 for(const auto * name : {"core:archangel", "core:titan", "core:goldDragon", "core:blackDragon", "core:ghostDragon", "core:ancientBehemoth"})
  ASSERT_TRUE(hero->setCreature(SlotID(slot++), CreatureID(CreatureID::decode(name)), 100));
 common->clearSlots(); ASSERT_TRUE(common->setCreature(SlotID(0), rogue, 5));
 const auto before = gameState()->saveToMemory();
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, common));
 EXPECT_EQ(gameState()->saveToMemory(), before);
 ASSERT_TRUE(hero->setCreature(SlotID(0), rogue, 1000));
 const auto matching = gameState()->saveToMemory();
 EXPECT_GT(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, common));
 EXPECT_EQ(gameState()->saveToMemory(), matching);
}

TEST_F(NewHorizonsProspectorAITest, PendingCompanyDebtWithoutDeliveredGuardsHasNoArmyReward)
{
 ASSERT_NO_FATAL_FAILURE(prepare());
 common->clearSlots(); int slot = 0;
 for(const auto * name : {"core:archangel", "core:titan", "core:goldDragon", "core:blackDragon", "core:ghostDragon", "core:ancientBehemoth", "core:archDevil"})
  ASSERT_TRUE(common->setCreature(SlotID(slot++), CreatureID(CreatureID::decode(name)), 1));
 GameHandlerTestServer server(gameState(), PLAYER); CGameHandler handler(server, gameState());
 common->grantWeeklyMercenaries(handler, 0, 10);
 ASSERT_EQ(common->getPendingMercenaries(), 10);
 common->clearSlots();
 EXPECT_EQ(NK2AI::ownedMineArmyReward(gateway->nullkiller.get(), hero, common), 0u);
 EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, common));
}
