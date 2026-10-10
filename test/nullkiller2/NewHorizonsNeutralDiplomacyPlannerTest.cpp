/*
 * NewHorizonsNeutralDiplomacyPlannerTest.cpp, part of VCMI engine
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
#include "lib/CCreatureHandler.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapping/CMap.h"
#include "lib/mapObjects/CGCreature.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "mock/GameHandlerTestServer.h"
#include "server/CGameHandler.h"
#include <shared_mutex>
#include <iomanip>
#include <sstream>

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER(1);

class NeutralOfferServer final : public GameHandlerTestServer
{
public:
	std::vector<BlockingDialog> dialogs;
	explicit NeutralOfferServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER) {}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * dialog = dynamic_cast<const BlockingDialog *>(&pack))
			dialogs.push_back(*dialog);
		GameHandlerTestServer::applyPack(pack);
	}
};

class NeutralMoveLoopback final : public IClient
{
	CGameHandler & handler;
	int requestId = 0;
public:
	int movements = 0;
	explicit NeutralMoveLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		if(dynamic_cast<const MoveHero *>(&request))
			++movements;
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++requestId;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return requestId;
	}
};

class NewHorizonsNeutralDiplomacyPlannerTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGCreature * neutral = nullptr;
	std::unique_ptr<NeutralOfferServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<NeutralMoveLoopback> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	std::string plannerDiagnostics;
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}
	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		const auto unit = CreatureID(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OTHER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.heroGarrison({{unit, 10}})
			.hero({30, 30, 0}, HeroTypeID(HeroTypeID::decode("core:adela")), OTHER)
			.heroGarrison({{unit, 1}})
			.monster({9, 5, 0}, unit, 2, static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		neutral = findFirst<CGCreature>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(neutral, nullptr);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:diplomacy")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		server = std::make_unique<NeutralOfferServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		handler->setMovePoints(hero->id, 2000);
		handler->giveResource(PLAYER, EGameResID::GOLD, 10000);
		revealMap(PLAYER);
		client = std::make_unique<NeutralMoveLoopback>(*handler);
		gateway = makeGateway(PLAYER, client.get());
	}
	std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> plan(float & selectedScore)
	{
		auto * ai = gateway->nullkiller.get();
		// Ordinary updateState initializes threats before path/goal evaluation.
		// Preserve the gateway's complete visible-object memory until then;
		// the neutral-only capture target scope is installed below.
		ai->dangerHitMap->updateHitMap();
		ai->dangerHitMap->calculateTileOwners();
		EXPECT_TRUE(ai->dangerHitMap->isHitMapUpToDate());
		EXPECT_TRUE(ai->dangerHitMap->isTileOwnersUpToDate());
		ai->heroManager->update();
		ai->armyManager->update();
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {neutral->id};
		ai->memory->alreadyVisited.clear();
		ai->objectClusterizer->reset();
		ai->objectClusterizer->clusterize();
		std::ostringstream diagnostic;
		diagnostic << std::setprecision(9) << "\nExpected hero=" << hero->id.getNum()
			<< " owner=" << hero->getOwner().getNum() << " at=" << hero->visitablePos().toString()
			<< " target=" << neutral->id.getNum() << " at=" << neutral->visitablePos().toString()
			<< " objectGraph=" << ai->isObjectGraphAllowed() << '\n';
		const auto describeCluster = [&diagnostic](const char * name, const auto & objects)
		{
			diagnostic << name << " count=" << objects.size() << ':';
			for(const auto * object : objects)
				diagnostic << " [id=" << object->id.getNum() << " type=" << object->ID.getNum()
					<< " at=" << object->visitablePos().toString() << ']';
			diagnostic << '\n';
		};
		describeCluster("near", ai->objectClusterizer->getNearbyObjects());
		describeCluster("far", ai->objectClusterizer->getFarObjects());
		const auto locked = ai->objectClusterizer->getLockedClusters();
		diagnostic << "locked count=" << locked.size() << '\n';
		for(const auto & cluster : locked)
		{
			diagnostic << "locked blocker=" << (cluster->blocker ? cluster->blocker->id.getNum() : -1) << " objects:";
			for(const auto & entry : cluster->objects)
				diagnostic << ' ' << entry.first.getNum();
			diagnostic << '\n';
		}
		std::vector<NK2AI::AIPath> routes;
		ai->pathfinder->calculatePathInfo(routes, neutral->visitablePos(), ai->isObjectGraphAllowed());
		diagnostic << "routes count=" << routes.size() << '\n';
		for(const auto & route : routes)
		{
			diagnostic << "route hero=" << (route.targetHero ? route.targetHero->id.getNum() : -1)
				<< " target=" << route.targetTile().toString() << " danger=" << route.getTotalDanger()
				<< " loss=" << route.getTotalArmyLoss() << " movement=" << route.movementCost()
				<< " turn=" << unsigned(route.turn()) << " exchanges=" << unsigned(route.exchangeCount);
			const auto blocked = route.getFirstBlockedAction();
			diagnostic << " blocked=" << bool(blocked) << " detail=" << route.toString() << '\n';
		}
		selectedScore = 0;
		std::shared_ptr<NK2AI::Goals::ExecuteHeroChain> selected;
		const auto goals = NK2AI::Goals::CaptureObjectsBehavior().decompose(ai);
		diagnostic << "goals count=" << goals.size() << '\n';
		for(const auto & goal : goals)
		{
			const std::shared_ptr<NK2AI::Goals::AbstractGoal> abstract = goal;
			auto path = std::dynamic_pointer_cast<NK2AI::Goals::ExecuteHeroChain>(abstract);
			diagnostic << "goal type=" << int(goal->goalType) << " description=" << goal->toString();
			if(path)
				diagnostic << " hero=" << (path->getPath().targetHero ? path->getPath().targetHero->id.getNum() : -1)
					<< " target=" << path->getPath().targetTile().toString();
			const float score = goal->invalid() ? 0 : ai->priorityEvaluator->evaluate(goal);
			if(!goal->invalid())
			{
				diagnostic << " score=" << score;
				const auto context = ai->priorityEvaluator->buildEvaluationContext(goal);
				diagnostic << " objid=" << goal->objid
					<< " visibleTarget=" << bool(ai->cc->getObj(ObjectInstanceID(goal->objid), false))
					<< " contextArmyReward=" << context.armyReward
					<< " contextGoldCost=" << context.goldCost
					<< " contextSkillReward=" << context.skillReward
					<< " contextArmyInvolvement=" << context.armyInvolvement
					<< " contextLoss=" << context.armyLossRatio
					<< " contextPower=" << context.powerRatio
					<< " maxLossTarget=" << ai->settings->getMaxArmyLossTarget()
					<< " enemyDangerRatio=" << context.enemyHeroDangerRatio
					<< " contextMovement=" << context.movementCost
					<< " closestRatio=" << context.closestWayRatio
					<< " contextRole=" << int(context.heroRole);
				if(path)
				{
					const auto * army = path->getPath().heroArmy;
					diagnostic << " pathArmyStrength=" << army->getArmyStrength()
						<< " pathArmyReward=" << context.evaluator.getArmyReward(neutral, hero, army, true)
						<< " pathArmyCost=" << context.evaluator.getGoldCost(neutral, hero, army);
				}
				for(int tier = NK2AI::PriorityEvaluator::BUILDINGS; tier <= NK2AI::PriorityEvaluator::MAX_PRIORITY_TIER; ++tier)
					diagnostic << " tier" << tier << '=' << ai->priorityEvaluator->evaluate(goal, tier, context);
			}
			diagnostic << '\n';
			if(!path || path->getPath().targetHero != hero || path->getPath().targetTile() != neutral->visitablePos())
				continue;
			if(score > selectedScore)
			{
				selectedScore = score;
				selected = path;
			}
		}
		plannerDiagnostics = diagnostic.str();
		return selected;
	}
};
}

TEST_F(NewHorizonsNeutralDiplomacyPlannerTest, OrdinaryPlannerIntentionallyReachesAffordableEligibleNeutralJoin)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.usesNewHorizonsRules);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_TRUE(forecast.willing);
	ASSERT_FALSE(forecast.authoredFree);
	ASSERT_EQ(forecast.thresholdPercent, 25);
	ASSERT_EQ(forecast.joiningAmount, 2);
	ASSERT_EQ(hero->getStackCount(SlotID(0)), 10);
	const auto capacity = hero->getLeadershipSlotCapacity(neutral->getCreatureID());
	ASSERT_TRUE(capacity);
	ASSERT_GE(capacity->maximum, 12);
	const auto gold = gateway->cc->getResourceAmount()[EGameResID::GOLD];
	ASSERT_GT(forecast.goldCost(), 0);
	ASSERT_GE(gold, forecast.goldCost());
	ASSERT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, neutral));
	const NK2AI::RewardEvaluator rewards(gateway->nullkiller.get());
	ASSERT_EQ(rewards.getArmyReward(neutral, hero, hero, true),
		static_cast<uint64_t>(neutral->getCreature()->getAIValue()) * forecast.joiningAmount);
	ASSERT_EQ(rewards.getGoldCost(neutral, hero, hero), forecast.goldCost());
	float score = 0;
	auto goal = plan(score);
	ASSERT_NE(goal, nullptr) << "No forced target, manual heroExchange/objectVisited, or authored-free exception" << plannerDiagnostics;
	ASSERT_GT(score, 0) << "Ordinary Nullkiller refuses nonpositive-priority tasks" << plannerDiagnostics;
	const auto neutralId = neutral->id;
	{
		std::shared_lock lock(CGameState::mutex);
		ASSERT_NO_THROW(goal->accept(gateway.get()));
	}
	ASSERT_GT(client->movements, 0);
	ASSERT_EQ(handler->getVisitingObject(hero), neutral);
	ASSERT_EQ(server->dialogs.size(), 1u);
	const auto & offer = server->dialogs.back();
	ASSERT_NE(offer.queryID, QueryID::NONE);
	// Respond to the genuine arrived offer separately: this witness tests
	// autonomous destination choice, not asynchronous UI-task delivery.
	gateway->cc->selectionMade(1, offer.queryID);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 12);
	EXPECT_EQ(gateway->cc->getResourceAmount()[EGameResID::GOLD], gold - forecast.goldCost());
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr);
}

TEST_F(NewHorizonsNeutralDiplomacyPlannerTest, UnaffordableOrInactiveNeutralJoinHasNoRecruitmentReward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const NK2AI::RewardEvaluator rewards(gateway->nullkiller.get());
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_TRUE(forecast.willing);
	ASSERT_GT(forecast.goldCost(), 0);
	ASSERT_GT(rewards.getArmyReward(neutral, hero, hero, true), 0u);
	const int gold = gateway->cc->getResourceAmount()[EGameResID::GOLD];
	handler->giveResource(PLAYER, EGameResID::GOLD, -gold);
	EXPECT_EQ(rewards.getArmyReward(neutral, hero, hero, true), 0u);
	EXPECT_EQ(rewards.getGoldCost(neutral, hero, hero), 0);
	handler->giveResource(PLAYER, EGameResID::GOLD, gold);
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:diplomacy")),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(neutral->getNewHorizonsDiplomacyForecast(*hero).willing);
	EXPECT_EQ(rewards.getArmyReward(neutral, hero, hero, true), 0u);
	EXPECT_EQ(rewards.getGoldCost(neutral, hero, hero), 0);
}
