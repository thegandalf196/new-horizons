/*
 * NewHorizonsAcademicStudyAITest.cpp, part of VCMI engine
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
#include "lib/callback/CCallback.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/mapObjects/NewHorizonsAcademicStudy.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto LEARNING = "new-horizons:learning";
class NewHorizonsAcademicStudyAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGTownInstance * rewardTown = nullptr;
	CGTownInstance * emptyTown = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(27), 30}})
			.town({10, 5, 0}, FactionID::TOWER, PLAYER).townGarrison({})
			.town({10, 15, 0}, FactionID::TOWER, PLAYER).townGarrison({});
		startWithMap(std::move(builder));
		revealMap(PLAYER);
		hero = findHeroAt({4, 5, 0});
		rewardTown = expectAt<CGTownInstance>({10, 5, 0});
		emptyTown = expectAt<CGTownInstance>({10, 15, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(rewardTown, nullptr);
		ASSERT_NE(emptyTown, nullptr);
		for(int index = 0; index < 5; ++index)
		{
			rewardTown->removeBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + index));
			emptyTown->removeBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + index));
		}
		for(int index = 0; index < 3; ++index)
			rewardTown->addBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + index));
		if(selected)
		{
			const SecondarySkill skill(SecondarySkill::decode(LEARNING));
			hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({LEARNING, "new-horizons:learning.eagleEye"});
			hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({LEARNING, "new-horizons:learning.academicStudy"});
			ASSERT_TRUE(hero->hasActivePerk(LEARNING, "new-horizons:learning.academicStudy"));
		}
		gateway = makeGateway(PLAYER);
	}
	std::vector<const CGObjectInstance *> candidates()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update();
		ai->armyManager->update();
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {rewardTown->id, emptyTown->id};
		ai->memory->alreadyVisited = {rewardTown->id, emptyTown->id};
		ai->objectClusterizer->reset();
		ai->objectClusterizer->clusterize();
		return ai->objectClusterizer->getNearbyObjects();
	}
};
}

TEST_F(NewHorizonsAcademicStudyAITest, ActualPlannerAdmitsOnlyHeroSpecificUnvisitedGuildReward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, rewardTown));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, emptyTown));
	EXPECT_FLOAT_EQ(reward.getSkillReward(rewardTown, hero, NK2AI::MAIN),
		static_cast<float>(hero->calculateXp(750)) / (1000.0f * std::sqrt(hero->level)));
	const auto result = candidates();
	EXPECT_NE(std::ranges::find(result, rewardTown), result.end());
	EXPECT_EQ(std::ranges::find(result, emptyTown), result.end());
}

TEST_F(NewHorizonsAcademicStudyAITest, RecordedTownHasNoRepeatXpOrPlannerCandidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ChangeObjectVisitors marker(ChangeObjectVisitors::VISITOR_ADD_HERO_ONLY, rewardTown->id, hero->id);
	gameState()->apply(marker);
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, rewardTown));
	EXPECT_FLOAT_EQ(reward.getSkillReward(rewardTown, hero, NK2AI::MAIN), 0.0f);
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsAcademicStudyAITest, UnselectedHeroDoesNotCreateOwnedTownCaptureLoop)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, rewardTown));
	EXPECT_EQ(newHorizonsLearning::academicStudyExperience(*hero, *rewardTown), 0);
	EXPECT_TRUE(candidates().empty());
}
