/*
 * NewHorizonsTownMusterAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/GameConstants.h"
#include "lib/GameSettings.h"
#include "lib/callback/IClient.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapping/CMap.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "mock/GameHandlerTestServer.h"
#include "server/CGameHandler.h"
#include <shared_mutex>

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER(1);
constexpr auto RECRUITMENT = "new-horizons:recruitment";
CreatureID creature(const char * key) { return CreatureID(CreatureID::decode(key)); }

class TownMusterLoopback final : public IClient
{
	CGameHandler & handler;
	int requestId = 0;
public:
	int movements = 0;
	std::vector<MusterCreatures> musterRequests;
	explicit TownMusterLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		if(dynamic_cast<const MoveHero *>(&request))
			++movements;
		if(const auto * muster = dynamic_cast<const MusterCreatures *>(&request))
			musterRequests.push_back(*muster);
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++requestId;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return requestId;
	}
};

class NewHorizonsTownMusterAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<TownMusterLoopback> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void rank(int value)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(RECRUITMENT)), value, ChangeValueMode::ABSOLUTE);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OTHER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({5, 20, 0}, HeroTypeID(1), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({30, 30, 0}, HeroTypeID(2), OTHER).heroGarrison({{CreatureID(0), 1}})
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER);
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0}); otherHero = findHeroAt({5, 20, 0});
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(otherHero, nullptr);
		ASSERT_NE(town, nullptr);
		town->clearSlots();
		town->creatures = {{0, {creature("core:pikeman")}}};
		town->addBuilding(BuildingID::DWELL_LVL_1);
		rank(MasteryLevel::BASIC);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		handler->setMovePoints(hero->id, 3000);
		revealMap(PLAYER);
		client = std::make_unique<TownMusterLoopback>(*handler);
		gateway = makeGateway(PLAYER, client.get());
	}
	int week() const
	{
		const auto calendar = gateway->cc->getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}
	std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> plan()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update(); ai->armyManager->update();
		NK2AI::PathfinderSettings settings; settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {town->id};
		ai->memory->alreadyVisited = {town->id};
		ai->objectClusterizer->reset(); ai->objectClusterizer->clusterize();
		float best = -1;
		std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> result;
		for(const auto & goal : NK2AI::Goals::CaptureObjectsBehavior().decompose(ai))
		{
			const std::shared_ptr<NK2AI::Goals::AbstractGoal> abstract = goal;
			auto path = std::dynamic_pointer_cast<NK2AI::Goals::ExecuteHeroChain>(abstract);
			if(!path || path->getPath().targetHero != hero)
				continue;
			const float score = ai->priorityEvaluator->evaluate(goal);
			if(score > best) { best = score; result = path; }
		}
		return result;
	}
	void execute(const std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> & goal)
	{
		std::shared_lock lock(CGameState::mutex);
		goal->accept(gateway.get());
	}
};
}

TEST_F(NewHorizonsTownMusterAITest, EmptyOwnedTownPlansMovesAndAutomaticallySubmitsRealBasicMuster)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(town->getVisitingHero(), nullptr);
	ASSERT_EQ(town->getGarrisonHero(), nullptr);
	EXPECT_FALSE(hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.academicStudy"));
	const uint64_t expected = uint64_t(2) * creature("core:pikeman").toCreature()->getAIValue();
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), expected);
	auto goal = plan();
	ASSERT_NE(goal, nullptr);
	ASSERT_NO_THROW(execute(goal));
	EXPECT_GT(client->movements, 0);
	ASSERT_EQ(town->getVisitingHero(), hero);
	ASSERT_EQ(client->musterRequests.size(), 1u);
	EXPECT_EQ(town->creatures[0].first, 2u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(week()), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), week());
	// Stock creation does not bypass recruitment, payment or Leadership.
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	gateway->performObjectInteraction(town, NK2AI::HeroPtr(hero, gateway->cc.get()));
	EXPECT_EQ(client->musterRequests.size(), 1u);
	EXPECT_EQ(town->creatures[0].first, 2u);
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), 0u);
	EXPECT_EQ(plan(), nullptr);
	gameState()->day = 8;
	EXPECT_EQ(week(), 1);
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), expected);
}

TEST_F(NewHorizonsTownMusterAITest, MissingRankUsedSettlementPendingAndOtherHeroKeepVisitGatesClosed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	EXPECT_FALSE(NK2AI::shouldVisit(ai, otherHero, town));
	rank(MasteryLevel::NONE);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, town));
	EXPECT_EQ(plan(), nullptr);
	rank(MasteryLevel::BASIC);
	ASSERT_TRUE(gateway->reserveMuster(hero, town, week(), 0, 1));
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), 0u);
	gateway->releasePendingMuster(hero, town, week());
	town->markNewHorizonsMusterUsed(week());
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, town));
	town->markNewHorizonsMusterUsed(-1);
	hero->markNewHorizonsMusterUsed(week());
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, town));
}

TEST_F(NewHorizonsTownMusterAITest, MasterRecruiterRetainsOneUseButCannotRevisitTheSameTown)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hero->level = 20;
	rank(MasteryLevel::EXPERT);
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.volunteerNetwork"});
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.eliteDraft"});
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.masterRecruiter"});
	ASSERT_TRUE(hero->hasActivePerk(RECRUITMENT, "new-horizons:recruitment.masterRecruiter"));
	hero->markNewHorizonsMusterUsed(week(), 1);
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), uint64_t(8) * creature("core:pikeman").toCreature()->getAIValue());
	town->markNewHorizonsMusterUsed(week());
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), 0u);
	town->markNewHorizonsMusterUsed(-1);
	hero->markNewHorizonsMusterUsed(week(), 2);
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), 0u);
}

TEST_F(NewHorizonsTownMusterAITest, CanonicalRankAmountsAndChampionCallShareExistingCandidateSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hero->level = 20;
	auto * ai = gateway->nullkiller.get();
	rank(MasteryLevel::ADVANCED);
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), uint64_t(4) * creature("core:pikeman").toCreature()->getAIValue());
	town->creatures = {{0, {creature("core:griffin")}}};
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), uint64_t(creature("core:griffin").toCreature()->getAIValue()));
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.volunteerNetwork"});
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.eliteDraft"});
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), uint64_t(2) * creature("core:griffin").toCreature()->getAIValue());
	rank(MasteryLevel::EXPERT);
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.championSCall"});
	town->creatures = {{0, {creature("core:angel")}}};
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), uint64_t(2) * creature("core:angel").toCreature()->getAIValue());
	rank(MasteryLevel::BASIC);
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), 0u);
}

TEST_F(NewHorizonsTownMusterAITest, NoGoldNoLeadershipHeadroomAndForeignTownProduceNoMusterDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	handler->giveResource(PLAYER, EGameResID::GOLD, -gateway->cc->getResourceAmount()[EGameResID::GOLD]);
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), 0u);
	EXPECT_EQ(plan(), nullptr);
	handler->giveResource(PLAYER, EGameResID::GOLD, 10000);
	const auto unit = creature("core:pikeman");
	const auto capacity = hero->getLeadershipSlotCapacity(unit);
	ASSERT_TRUE(capacity);
	hero->clearSlots();
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), unit, capacity->maximum));
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), 0u);
	ASSERT_TRUE(hero->setCreature(SlotID(0), unit, capacity->maximum - 1));
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), uint64_t(unit.toCreature()->getAIValue()));
	handler->setOwner(town, OTHER);
	ASSERT_EQ(town->getOwner(), OTHER);
	EXPECT_EQ(NK2AI::townMusterArmyReward(ai, hero, town), 0u);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, town)); // ordinary enemy capture policy
}

TEST_F(NewHorizonsTownMusterAITest, BroadMusterGeneratedPathSubmitsExactSplitWithoutMultiplyingTheUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	rank(MasteryLevel::ADVANCED);
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.broadMuster"});
	ASSERT_TRUE(hero->hasActivePerk(RECRUITMENT, "new-horizons:recruitment.broadMuster"));
	const auto first = creature("core:pikeman");
	const auto second = creature("core:archer");
	const auto filler = creature("core:griffin");
	town->creatures = {{0, {first}}, {0, {second}}};
	const auto firstCap = hero->getLeadershipSlotCapacity(first);
	const auto secondCap = hero->getLeadershipSlotCapacity(second);
	const auto fillerCap = hero->getLeadershipSlotCapacity(filler);
	ASSERT_TRUE(firstCap);
	ASSERT_TRUE(secondCap);
	ASSERT_TRUE(fillerCap);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), first, firstCap->maximum - 1));
	ASSERT_TRUE(hero->setCreature(SlotID(1), second, secondCap->maximum - 1));
	for(int slot = 2; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), filler, fillerCap->maximum));
	EXPECT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town),
		uint64_t(first.toCreature()->getAIValue()) + second.toCreature()->getAIValue());
	auto goal = plan();
	ASSERT_NE(goal, nullptr);
	ASSERT_NO_THROW(execute(goal));
	ASSERT_EQ(client->musterRequests.size(), 1u);
	EXPECT_GT(town->creatures[0].first, 0u);
	EXPECT_GT(town->creatures[1].first, 0u);
	EXPECT_EQ(town->creatures[0].first + town->creatures[1].first, 4u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(week()), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), week());
}
