/*
 * NewHorizonsExternalMusterAITest.cpp, part of VCMI engine
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
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/ObjectTemplate.h"
#include "lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "lib/mapObjectConstructors/CObjectClassesHandler.h"
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
constexpr auto EXTERNAL = "new-horizons:recruitment.externalRecruiter";
CreatureID pikeman() { return CreatureID(CreatureID::decode("core:pikeman")); }

class DwellingRecordingServer final : public GameHandlerTestServer
{
public:
	std::vector<BlockingDialog> dialogs;
	std::vector<OpenWindow> windows;
	explicit DwellingRecordingServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER) {}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * dialog = dynamic_cast<const BlockingDialog *>(&pack))
			dialogs.push_back(*dialog);
		if(const auto * window = dynamic_cast<const OpenWindow *>(&pack))
			windows.push_back(*window);
		GameHandlerTestServer::applyPack(pack);
	}
};

class DwellingLoopback final : public IClient
{
	CGameHandler & handler;
	int requestId = 0;
public:
	int movements = 0;
	int musterRequests = 0;
	explicit DwellingLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		if(dynamic_cast<const MoveHero *>(&request))
			++movements;
		if(dynamic_cast<const MusterCreatures *>(&request))
			++musterRequests;
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++requestId;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return requestId;
	}
};

class NewHorizonsExternalMusterAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
	CGDwelling * dwelling = nullptr;
	std::unique_ptr<DwellingRecordingServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<DwellingLoopback> client;
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
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OTHER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.hero({30, 30, 0}, HeroTypeID(2), OTHER).heroGarrison({{CreatureID(0), 1}})
			.dwelling({8, 8, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));
		hero = findHeroAt({4, 5, 0}); otherHero = findHeroAt({4, 20, 0});
		dwelling = dynamic_cast<CGDwelling *>(findObjectAt({8, 8, 0}));
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(otherHero, nullptr);
		ASSERT_NE(dwelling, nullptr);
		dwelling->clearSlots();
		dwelling->creatures = {{0, {pikeman()}}};
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(RECRUITMENT)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			hero->applyPerkSelection({RECRUITMENT, EXTERNAL});
			ASSERT_TRUE(hero->hasActivePerk(RECRUITMENT, EXTERNAL));
		}
		server = std::make_unique<DwellingRecordingServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		handler->setMovePoints(hero->id, 2000);
		revealMap(PLAYER);
		client = std::make_unique<DwellingLoopback>(*handler);
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
		ai->memory->visitableObjs = {dwelling->id};
		ai->memory->alreadyVisited = {dwelling->id};
		ai->objectClusterizer->reset(); ai->objectClusterizer->clusterize();
		float best = -1;
		std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> selected;
		for(const auto & goal : NK2AI::Goals::CaptureObjectsBehavior().decompose(ai))
		{
			const std::shared_ptr<NK2AI::Goals::AbstractGoal> abstract = goal;
			auto path = std::dynamic_pointer_cast<NK2AI::Goals::ExecuteHeroChain>(abstract);
			if(!path || path->getPath().targetHero != hero)
				continue;
			const float score = ai->priorityEvaluator->evaluate(goal);
			if(score > best) { best = score; selected = path; }
		}
		return selected;
	}
};
}

TEST_F(NewHorizonsExternalMusterAITest, EmptyOwnedCorePlansMovesAndUsesOrdinaryRecruitmentQuery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(hero->hasActivePerk(RECRUITMENT, "new-horizons:recruitment.recruiterSContacts"));
	const auto expectedValue = uint64_t(2) * pikeman().toCreature()->getAIValue();
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), expectedValue);
	auto goal = plan();
	ASSERT_NE(goal, nullptr);
	ASSERT_EQ(goal->getPath().targetTile(), dwelling->visitablePos());
	{
		std::shared_lock lock(CGameState::mutex);
		ASSERT_NO_THROW(goal->accept(gateway.get()));
	}
	ASSERT_GT(client->movements, 0);
	ASSERT_FALSE(server->dialogs.empty());
	ASSERT_EQ(handler->getVisitingObject(hero), dwelling);
	// Deliver the real query's ordinary acceptance; never inject a visit or stock receipt.
	gateway->cc->selectionMade(1, server->dialogs.back().queryID);
	ASSERT_FALSE(server->windows.empty());
	const auto window = server->windows.back();
	ASSERT_EQ(window.object, dwelling->id);
	ASSERT_EQ(window.visitor, hero->id);
	ASSERT_EQ(window.window, EOpenWindowMode::RECRUITMENT_FIRST);
	// These are exactly the existing showRecruitmentDialog task's synchronous body.
	gateway->tryExternalMusterCreatures(hero, dwelling);
	ASSERT_EQ(client->musterRequests, 1);
	EXPECT_EQ(dwelling->creatures[0].first, 2u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(week()), 1);
	EXPECT_EQ(dwelling->getNewHorizonsMusterLastWeek(), week());
	gateway->recruitCreatures(dwelling, hero);
	gateway->cc->selectionMade(0, window.queryID);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(dwelling->creatures[0].first, 0u);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
	EXPECT_EQ(plan(), nullptr);
	gameState()->day = 8;
	EXPECT_EQ(week(), 1);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), expectedValue);
}

TEST_F(NewHorizonsExternalMusterAITest, NoPerkOtherHeroAndSpentHeroCannotVisitForEmptyMuster)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, dwelling));
	EXPECT_EQ(plan(), nullptr);
	hero->applyPerkSelection({RECRUITMENT, EXTERNAL});
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, dwelling));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), otherHero, dwelling));
	hero->markNewHorizonsMusterUsed(week());
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, dwelling));
	EXPECT_EQ(plan(), nullptr);
}

TEST_F(NewHorizonsExternalMusterAITest, SettlementUsePendingAndForeignOwnershipNeverBypassGates)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	ASSERT_TRUE(gateway->reserveMuster(hero, dwelling, week(), 0, 1));
	EXPECT_EQ(NK2AI::externalMusterArmyReward(ai, hero, dwelling), 0u);
	gateway->releasePendingMuster(hero, dwelling, week());
	dwelling->markNewHorizonsMusterUsed(week());
	EXPECT_EQ(NK2AI::externalMusterArmyReward(ai, hero, dwelling), 0u);
	dwelling->markNewHorizonsMusterUsed(-1);
	dwelling->setOwner(OTHER);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(ai, hero, dwelling), 0u);
	// Ordinary enemy capture remains permitted, but receives no pre-capture Muster reward.
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, dwelling));
}

TEST_F(NewHorizonsExternalMusterAITest, ExactLeadershipHeadroomAndFreeCoreCostDetermineGeneratedValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman());
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	hero->clearSlots();
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), pikeman(), capacity->maximum));
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman(), capacity->maximum - 1));
	handler->giveResource(PLAYER, EGameResID::GOLD, -gateway->cc->getResourceAmount()[EGameResID::GOLD]);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), uint64_t(pikeman().toCreature()->getAIValue()));
}

TEST_F(NewHorizonsExternalMusterAITest, VisitedMultiRowDwellingNeedsCurrentOwnEligibleMusterHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto subtype = MapObjectSubID::decode(Obj::CREATURE_GENERATOR4, "core:elementalConflux");
	ASSERT_GE(subtype, 0);
	const auto objectHandler = LIBRARY->objtypeh->getHandlerFor(Obj::CREATURE_GENERATOR4, MapObjectSubID(subtype));
	ASSERT_NE(objectHandler, nullptr);
	const auto templates = objectHandler->getTemplates();
	ASSERT_FALSE(templates.empty());
	const auto tile = dwelling->visitablePos();
	map()->hideObject(dwelling);
	dwelling->pos = tile + templates.front()->getVisitableOffset();
	dwelling->appearance = templates.front();
	dwelling->ID = Obj::CREATURE_GENERATOR4;
	dwelling->subID = MapObjectSubID(subtype);
	map()->showObject(dwelling);
	dwelling->creatures = {{0, {pikeman()}}, {0, {pikeman()}}};
	ASSERT_EQ(dwelling->visitablePos(), tile);
	EXPECT_NE(plan(), nullptr);
	dwelling->markNewHorizonsMusterUsed(week());
	EXPECT_EQ(plan(), nullptr);
}

TEST_F(NewHorizonsExternalMusterAITest, MasterRecruiterAllowsOnlyOneRemainingUseAndNeverReusesSettlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(RECRUITMENT)), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.eliteDraft"});
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.masterRecruiter"});
	ASSERT_TRUE(hero->hasActivePerk(RECRUITMENT, "new-horizons:recruitment.masterRecruiter"));
	hero->markNewHorizonsMusterUsed(week(), 1);
	EXPECT_GT(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
	dwelling->markNewHorizonsMusterUsed(week());
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
	dwelling->markNewHorizonsMusterUsed(-1);
	hero->markNewHorizonsMusterUsed(week(), 2);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
}

TEST_F(NewHorizonsExternalMusterAITest, PaidCoreWithoutGoldDoesNotInventRecruitableMusterValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto archer = CreatureID(CreatureID::decode("core:archer"));
	ASSERT_EQ(archer.toCreature()->getLevel(), 2);
	const auto category = gateway->cc->getCreatureCategory(archer);
	ASSERT_TRUE(category);
	ASSERT_EQ(category->category, newHorizonsCreatures::CreatureCategory::CORE);
	dwelling->creatures = {{0, {archer}}};
	handler->giveResource(PLAYER, EGameResID::GOLD, -gateway->cc->getResourceAmount()[EGameResID::GOLD]);
	EXPECT_EQ(NK2AI::externalMusterArmyReward(gateway->nullkiller.get(), hero, dwelling), 0u);
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, dwelling));
	EXPECT_EQ(plan(), nullptr);
}
