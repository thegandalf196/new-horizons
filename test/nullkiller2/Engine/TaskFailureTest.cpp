/*
 * TaskFailureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinderConfig.h"
#include "AI/Nullkiller2/Pathfinding/Actions/BattleAction.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Goals/Composition.h"
#include "AI/Nullkiller2/Goals/Invalid.h"
#include "nullkiller2/NullkillerTest.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/MiscObjects.h"
#include "lib/CPlayerState.h"
#include "AI/Nullkiller2/Pathfinding/Actions/DimensionDoorAction.h"
#include "mock/GameHandlerTestServer.h"
#include "SpellPointTestUtils.h"
#include "lib/callback/IClient.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/ISpellMechanics.h"
#include "lib/spells/Problem.h"
#include "server/CGameHandler.h"

#include <shared_mutex>
#include <stdexcept>

namespace
{
template<typename T> std::vector<std::byte> routeSerialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class RouteServer final : public GameHandlerTestServer
{
public:
	explicit RouteServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PlayerColor(0)) {}
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<bool> accepted;
	int transports = 0;
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack);
		if(auto * movement = dynamic_cast<TryMoveHero *>(&pack); movement && gateway)
		{
			// Match the shipping client's ordering: invalidate before heroMoved.
			gateway->invalidatePaths();
			gateway->heroMoved(*movement, true);
			if(movement->result == TryMoveHero::TELEPORTATION)
				++transports;
		}
		else if(dynamic_cast<SetMovePoints *>(&pack) && gateway)
			gateway->invalidatePaths();
	}
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * reply = dynamic_cast<PackageApplied *>(&pack))
		{
			accepted.push_back(reply->result);
			gateway->requestRealized(reply);
		}
	}
};

class RouteClient final : public IClient
{
	CGameHandler & handler;
	CRandomGenerator & rng;
	int sequence = 0;
public:
	NK2AI::AIGateway * gateway = nullptr;
	int casts = 0;
	int moves = 0;
	std::pair<std::vector<std::byte>, std::vector<std::byte>> completedRng;
	RouteClient(CGameHandler & handler, CRandomGenerator & rng) : handler(handler), rng(rng) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++sequence;
		if(dynamic_cast<CastAdvSpell *>(incoming.get()))
			++casts;
		else if(dynamic_cast<MoveHero *>(incoming.get()))
			++moves;
		else
			throw std::runtime_error("Route fixture refuses unrelated requests");
		gateway->requestSent(incoming.get(), sequence);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		completedRng = {routeSerialized(*handler.randomizer), routeSerialized(rng)};
		return sequence;
	}
};

class ContinuationProbe final : public NK2AI::Goals::ElementarGoal<ContinuationProbe>
{
	int * calls;
public:
	explicit ContinuationProbe(int & calls) : calls(&calls) {}
	bool operator==(const ContinuationProbe & other) const override { return calls == other.calls; }
	std::string toString() const override { return "dependent exploration probe"; }
	void accept(NK2AI::AIGateway *) override { ++*calls; }
};
}

class Nullkiller2_MovementFailure : public NullkillerTest
{
protected:
	void checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero = false, bool useTownPurchase = false);

	CGHeroInstance * startHero()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("MovementFailure")
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(27), 1}});
		startWithMap(std::move(builder));
		revealMap(PlayerColor(0));
		return findHeroByOwner(PlayerColor(0));
	}
};

TEST_F(Nullkiller2_MovementFailure, unreachableDestinationFailsWithoutMoving)
{
	auto * hero = startHero();
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	const auto movement = hero->movementPointsRemaining();
	const auto target = position + int3(1, 0, 0);
	map()->getTile(target).terrainType = ETerrainId::ROCK;
	auto gateway = makeGateway(PlayerColor(0));
	CGPath path;
	ASSERT_FALSE(gateway->nullkiller->getPathsInfo(hero)->getPath(path, target));
	EXPECT_THROW(gateway->moveHeroToTile(target, NK2AI::HeroPtr(hero, gateway->cc.get())),
		NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
}

TEST_F(Nullkiller2_MovementFailure, validFutureDayDestinationIsPendingNotFailure)
{
	auto * hero = startHero();
	ASSERT_NE(hero, nullptr);
	hero->setMovementPoints(0);
	const auto position = hero->visitablePos();
	const auto target = position + int3(1, 0, 0);
	auto gateway = makeGateway(PlayerColor(0));
	CGPath path;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(path, target));
	ASSERT_FALSE(path.nodes.empty());
	EXPECT_GT(path.nodes.front().turns, 0);
	EXPECT_FALSE(gateway->moveHeroToTile(target, NK2AI::HeroPtr(hero, gateway->cc.get())));
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
}

void Nullkiller2_MovementFailure::checkRequiredBattleRoute(bool useGarrison, bool useEnemyHero, bool useTownPurchase)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("RequiredBattleRoute")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison(useTownPurchase
			? std::vector<std::pair<CreatureID, uint16_t>>{{CreatureID(0), 1}}
			: std::vector<std::pair<CreatureID, uint16_t>>{{CreatureID(27), 100}});
	if(useTownPurchase)
		builder.town({6, 3, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({});
	if(useEnemyHero)
		builder.playerActive(PlayerColor(1))
			.hero({10, 5, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
	else if(!useGarrison)
		builder.monster({10, 5, 0}, CreatureID(0), 1);
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 || (useTownPurchase && x < 9)
				? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	if(useTownPurchase)
	{
		auto * town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		town->addBuilding(BuildingID::DWELL_LVL_1);
		town->creatures.at(0) = {40, {CreatureID(0)}};
		grantResources(PlayerColor(0), GameResID(GameResID::GOLD), 100000);
	}
	if(useGarrison)
	{
		auto garrison = std::make_shared<CGGarrison>(gameState().get());
		garrison->id = ObjectInstanceID(static_cast<int>(map()->objects.size()));
		garrison->ID = Obj::GARRISON;
		garrison->subID = MapObjectSubID(0);
		garrison->tempOwner = PlayerColor::NEUTRAL;
		garrison->removableUnits = true;
		// Reuse visit-direction geometry only; this test does not render art.
		garrison->appearance = hero->appearance;
		const int3 guardPosition(10, 5, 0);
		garrison->pos = guardPosition + garrison->getVisitableOffset();
		ASSERT_TRUE(garrison->setCreature(SlotID(0), CreatureID(0), 1));
		map()->objects.push_back(garrison);
		map()->getTile(guardPosition).blockingObjects.push_back(garrison->id);
		map()->getTile(guardPosition).visitableObjects.push_back(garrison->id);
	}
	hero->setMovementPoints(20);
	auto gateway = makeGateway(PlayerColor(0));
	const int3 target(15, 5, 0);
	CGPath livePath;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(livePath, {6, 5, 0}));
	ASSERT_FALSE(gateway->nullkiller->getPathsInfo(hero)->getPath(livePath, target));
	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(hero, NK2AI::MAIN);
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = useTownPurchase;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(paths.empty());
	if(useTownPurchase)
	{
		ASSERT_TRUE(std::ranges::any_of(paths, [](const NK2AI::AIPath & path)
		{
			return path.exchangeCount > 1;
		}));
	}
	for(const auto & path : paths)
	{
		EXPECT_TRUE(std::ranges::any_of(path.nodes, [](const NK2AI::AIPathNodeInfo & node)
		{
			return dynamic_cast<const NK2AI::AIPathfinding::BattleAction *>(node.specialAction.get()) != nullptr;
		}));
	}
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(false);
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredGarrisonBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(true);
}

TEST_F(Nullkiller2_MovementFailure, projectedRoutePreservesRequiredEnemyHeroBattleBeforeDistantTarget)
{
	checkRequiredBattleRoute(false, true);
}

TEST_F(Nullkiller2_MovementFailure, townPurchaseRoutePreservesRequiredEnemyHeroBattle)
{
	checkRequiredBattleRoute(false, true, true);
}

TEST_F(Nullkiller2_MovementFailure, supersededBattleNodeCannotResumeWithoutBattleAction)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("SupersededBattleNode")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(27), 100}});
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto garrison = std::make_shared<CGGarrison>(gameState().get());
	garrison->id = ObjectInstanceID(static_cast<int>(map()->objects.size()));
	garrison->ID = Obj::GARRISON;
	garrison->subID = MapObjectSubID(0);
	garrison->tempOwner = PlayerColor::NEUTRAL;
	garrison->appearance = hero->appearance;
	garrison->pos = int3(10, 5, 0) + garrison->getVisitableOffset();
	ASSERT_TRUE(garrison->setCreature(SlotID(0), CreatureID(0), 10));
	map()->objects.push_back(garrison);
	map()->getTile({10, 5, 0}).blockingObjects.push_back(garrison->id);
	map()->getTile({10, 5, 0}).visitableObjects.push_back(garrison->id);
	auto gateway = makeGateway(PlayerColor(0));
	auto storage = std::make_shared<NK2AI::AINodeStorage>(gateway->nullkiller.get(), gateway->cc->getMapSize());
	storage->clear();
	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(hero, NK2AI::MAIN);
	storage->setHeroes(heroes);
	auto config = std::make_shared<NK2AI::AIPathfinding::AIPathfinderConfig>(gateway->nullkiller.get(), storage, true, false);
	gateway->cc->calculatePaths(config);
	int superseded = 0;
	int battleAware = 0;
	storage->iterateValidNodes({10, 5, 0}, EPathfindingLayer::LAND, [&](NK2AI::AIPathNode & node)
	{
		if(node.action == EPathNodeAction::BATTLE && node.specialAction)
			++battleAware;
		else if(!node.actor->allowBattle && node.turns != 0xff)
		{
			++superseded;
			EXPECT_EQ(node.action, EPathNodeAction::UNKNOWN);
			EXPECT_FALSE(node.locked);
			CDestinationNodeInfo cheaperApproach;
			cheaperApproach.node = &node;
			cheaperApproach.cost = node.getCost() - 0.01f;
			EXPECT_TRUE(cheaperApproach.isBetterWay());
		}
	});
	EXPECT_GT(superseded, 0);
	EXPECT_GT(battleAware, 0);
}

TEST_F(Nullkiller2_MovementFailure, townPurchaseRoutePreservesRequiredGarrisonBattle)
{
	checkRequiredBattleRoute(true, false, true);
}

TEST_F(Nullkiller2_MovementFailure, alliedHeroCanBlockPreviouslyPlannedCorridor)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("AlliedCorridorBlocker")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.hero({10, 8, 0}, HeroTypeID(1), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}});
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 || y == 8
				? ETerrainId::GRASS : ETerrainId::ROCK;
	auto gateway = makeGateway(PlayerColor(0));
	const CGHeroInstance * traveler = nullptr;
	const CGHeroInstance * blocker = nullptr;
	for(const auto * hero : gateway->cc->getHeroesInfo())
	{
		if(hero->visitablePos().y == 5)
			traveler = hero;
		else
			blocker = hero;
	}
	ASSERT_NE(traveler, nullptr);
	ASSERT_NE(blocker, nullptr);
	const int3 target(15, 5, 0);
	CGPath live;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(traveler, NK2AI::MAIN);
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = false;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	const auto retainedPlans = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(retainedPlans.empty());
	const NK2AI::Goals::ExecuteHeroChain queuedRoute(retainedPlans.front());
	NK2AI::Goals::Composition queuedComposition;
	queuedComposition.addNext(queuedRoute);
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	EXPECT_EQ(queuedComposition.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	const auto original = blocker->pos;
	map()->moveObject(blocker->id, int3(10, 5, 0) + blocker->getVisitableOffset());
	gateway->nullkiller->invalidatePaths();
	EXPECT_FALSE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), traveler);
	EXPECT_EQ(queuedComposition.getBlockedInitialRoute(gateway->nullkiller.get()), traveler);
	EXPECT_FALSE(gateway->nullkiller->isHeroLocked(traveler));
	// An earlier task or special action must execute before a later route can
	// be judged; it may remove the obstruction or transport the hero.
	NK2AI::Goals::Composition earlierTask;
	earlierTask.addNextSequence({NK2AI::Goals::sptr(NK2AI::Goals::Invalid()), NK2AI::Goals::sptr(queuedRoute)});
	EXPECT_EQ(earlierTask.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	auto specialPath = retainedPlans.front();
	specialPath.nodes.back().specialAction = std::make_shared<NK2AI::AIPathfinding::BattleAction>(target);
	EXPECT_EQ(NK2AI::Goals::ExecuteHeroChain(specialPath).getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	// A copied plan remains a historical plan, not proof that its route is still open.
	EXPECT_EQ(retainedPlans.front().targetTile(), target);
	map()->moveObject(blocker->id, original);
	gateway->nullkiller->invalidatePaths();
	EXPECT_TRUE(gateway->nullkiller->getPathsInfo(traveler)->getPath(live, target));
	EXPECT_EQ(queuedRoute.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
}

TEST_F(Nullkiller2_MovementFailure, perkMovementChangeRefreshesProjectedRoutesWithoutMovingHero)
{
	auto * hero = startHero();
	ASSERT_NE(hero, nullptr);
	if(!hero->usesNewHorizonsMovement())
		GTEST_SKIP() << "Requires New Horizons movement";
	const auto logistics = SecondarySkill(SecondarySkill::decode("new-horizons:logistics"));
	hero->setSecSkillLevel(logistics, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto position = hero->visitablePos();
	const int3 target = position + int3(2, 0, 0);
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
		{
			auto & tile = map()->getTile({x, y, 0});
			tile.terrainType = y == position.y ? ETerrainId::SAND : ETerrainId::ROCK;
			tile.roadType = RoadId::NO_ROAD;
		}
	auto gateway = makeGateway(PlayerColor(0));
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const auto before = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(before.empty());
	hero->applyPerkSelection({"new-horizons:logistics", "new-horizons:logistics.pathfinding"});
	ASSERT_TRUE(hero->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.pathfinding"));
	gateway->invalidatePaths();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const auto after = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(after.empty());
	EXPECT_LT(after.front().movementCost(), before.front().movementCost());
	EXPECT_EQ(hero->visitablePos(), position);
}

TEST_F(Nullkiller2_MovementFailure, armyChangeRefreshesGuardedRoutesWithoutMovingHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("ArmyChangeRoute")
		.playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.monster({10, 5, 0}, CreatureID(0), 100);
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	auto gateway = makeGateway(PlayerColor(0));
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const int3 target(15, 5, 0);
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(27), 100));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const auto strongerPaths = gateway->nullkiller->pathfinder->getPathInfo(target);
	ASSERT_FALSE(strongerPaths.empty());
	for(const auto & path : strongerPaths)
	{
		EXPECT_TRUE(std::ranges::any_of(path.nodes, [](const NK2AI::AIPathNodeInfo & node)
		{
			return dynamic_cast<const NK2AI::AIPathfinding::BattleAction *>(node.specialAction.get()) != nullptr;
		}));
	}
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(0), 1));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 3));
	EXPECT_TRUE(gateway->nullkiller->pathfinder->getPathInfo(target).empty());
	EXPECT_EQ(hero->visitablePos(), position);
}

TEST_F(Nullkiller2_MovementFailure, armyChangeRefreshesEnemyRoutesThroughStationaryHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("ArmyChangeThreat")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({10, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}})
		.hero({5, 5, 0}, HeroTypeID(1), PlayerColor(1))
		.heroGarrison({{CreatureID(0), 100}});
	startWithMap(std::move(builder));
	revealMap(PlayerColor(0));
	for(int x = 0; x < 36; ++x)
		for(int y = 0; y < 36; ++y)
			map()->getTile({x, y, 0}).terrainType = y == 5 ? ETerrainId::GRASS : ETerrainId::ROCK;
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto position = hero->visitablePos();
	auto gateway = makeGateway(PlayerColor(0));
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));
	const int3 target(15, 5, 0);
	EXPECT_GT(gateway->nullkiller->dangerHitMap->getTileThreat(target).maximumDanger.danger, 0);
	const auto * enemy = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(enemy, nullptr);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, hero), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, enemy), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), enemy), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), hero), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(hero), 0);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy), 0);
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(27), 100));
	gateway->garrisonsChanged(hero->id, ObjectInstanceID());
	EXPECT_FALSE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_FALSE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	EXPECT_EQ(gateway->nullkiller->dangerHitMap->getTileThreat(target).maximumDanger.danger, 0);
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isHitMapUpToDate());
	EXPECT_TRUE(gateway->nullkiller->dangerHitMap->isTileOwnersUpToDate());
	EXPECT_EQ(hero->visitablePos(), position);
	CGHeroInstance neutralVisitor(gameState().get());
	neutralVisitor.setOwner(PlayerColor::NEUTRAL);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, &neutralVisitor), 0);
	gameState()->getPlayerTeam(PlayerColor(1))->players.erase(PlayerColor(1));
	gameState()->getPlayerTeam(PlayerColor(0))->players.insert(PlayerColor(1));
	gameState()->getPlayerState(PlayerColor(1))->team = gameState()->getPlayerState(PlayerColor(0))->team;
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(position, enemy), 0);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(enemy->visitablePos(), hero), 0);
}

TEST(Nullkiller2_Engine_TaskFailure, triesNextTaskWhenAnotherCandidateIsAvailable)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, true, false),
		NK2AI::TaskFailureAction::TRY_NEXT_TASK);
}

TEST(Nullkiller2_Engine_TaskFailure, replansAfterPreviousProgressEvenWithRemainingTasks)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(true, true, false),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, replansAfterPreviousProgress)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(true, false, false),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, replansWhenAnotherHeroCanStillMove)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, false, true),
		NK2AI::TaskFailureAction::REPLAN);
}

TEST(Nullkiller2_Engine_TaskFailure, stopsWhenNoProgressOrAlternativeExists)
{
	EXPECT_EQ(
		NK2AI::chooseTaskFailureAction(false, false, false),
		NK2AI::TaskFailureAction::STOP_TURN);
}

class Nullkiller2_TransportContinuation : public Nullkiller2_MovementFailure
{
protected:
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<RouteServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<RouteClient> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	const int3 landing{8, 5, 0};
	const int3 missingTarget{13, 5, 0};
	void TearDown() override
	{
		if(server)
			server->gateway = nullptr;
		gateway.reset();
		client.reset();
		handler.reset();
		server.reset();
		Nullkiller2_MovementFailure::TearDown();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		// New Horizons DD spends all MP. Historical saved rules give this
		// authoritative transport a genuine positive-MP continuation boundary.
		JsonNode plainRules;
		plainRules.Struct();
		plainRules.setOverrideFlag(true);
		for(const auto setting : {EGameSettings::MAGIC_NEW_HORIZONS, EGameSettings::HEROES_NEW_HORIZONS,
			EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
			EGameSettings::HEROES_NEW_HORIZONS_PERKS})
			loaded->overrideGameSetting(setting, plainRules);
	}
	void prepareTransport()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("UnavailablePostTransportRoute")
			.playerActive(PlayerColor(0)).hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}}).heroPrimary(10, 10, 10, 20)
			.heroSpells({SpellID(8)})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
				map()->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
		map()->getTile(missingTarget).terrainType = ETerrainId::ROCK;
		revealMap(PlayerColor(0));
		setTestSpellPointTotal(hero, 200);
		hero->setMovementPoints(1000);
		gameState()->actingPlayers.insert(PlayerColor(0));
		server = std::make_unique<RouteServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		client = std::make_unique<RouteClient>(*handler, dynamic_cast<CRandomGenerator &>(*getRNG()));
		gateway = makeGateway(PlayerColor(0), client.get());
		server->gateway = client->gateway = gateway.get();
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(SpellID(8).toSpell()->getAdventureMechanics().canBeCastAt(
			problem, gateway->cc.get(), hero, landing));
		ASSERT_FALSE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));
	}
	NK2AI::AIPath transportPath(const int3 & target) const
	{
		NK2AI::AIPath path;
		path.targetHero = hero;
		path.heroArmy = hero;
		path.chainMask = 1;
		path.exchangeCount = 0;
		path.targetObjectDanger = path.armyLoss = path.targetObjectArmyLoss = 0;
		NK2AI::AIPathNodeInfo future{};
		future.targetHero = hero;
		future.coord = target;
		future.turns = 3;
		future.cost = 3.f;
		future.chainMask = 1;
		future.parentIndex = -1;
		future.layer = EPathfindingLayer::LAND;
		auto transport = future;
		transport.coord = landing;
		transport.turns = 0;
		transport.cost = 0.1f;
		transport.parentIndex = 0;
		NK2AI::AIPathfinding::DimensionDoorActionParameters parameters;
		parameters.usedSpell = SpellID(8);
		parameters.destination = landing;
		transport.specialAction = std::make_shared<NK2AI::AIPathfinding::DimensionDoorAction>(parameters);
		path.nodes = {future, transport};
		return path;
	}
	auto rngState()
	{
		return std::pair(routeSerialized(*handler->randomizer),
			routeSerialized(dynamic_cast<CRandomGenerator &>(*getRNG())));
	}
	NK2AI::TaskExecutionResult executeChain(const NK2AI::AIPath & path)
	{
		std::shared_lock lock(CGameState::mutex);
		return NK2AI::executeTask(*gateway, NK2AI::Goals::taskptr(NK2AI::Goals::ExecuteHeroChain(path)));
	}
};

TEST_F(Nullkiller2_TransportContinuation, realTransportThenMissingFutureLegAbortsDependentComposition)
{
	ASSERT_NO_FATAL_FAILURE(prepareTransport());
	const auto mana = hero->getManaAvailable();
	const auto cost = hero->getSpellCost(SpellID(8).toSpell());
	auto path = transportPath(missingTarget);
	NK2AI::Goals::ExecuteHeroChain chain(path);
	EXPECT_EQ(chain.getBlockedInitialRoute(gateway->nullkiller.get()), nullptr);
	int dependentCalls = 0;
	NK2AI::Goals::Composition composition;
	composition.addNextSequence({NK2AI::Goals::sptr(chain), NK2AI::Goals::sptr(ContinuationProbe(dependentCalls))});
	NK2AI::TaskExecutionResult execution{true, std::nullopt};
	{
		std::shared_lock lock(CGameState::mutex);
		execution = NK2AI::executeTask(*gateway, NK2AI::Goals::taskptr(composition));
	}
	EXPECT_FALSE(execution.success);
	ASSERT_TRUE(execution.unavailableRouteHero.has_value());
	EXPECT_EQ(*execution.unavailableRouteHero, hero->id);
	EXPECT_EQ(hero->visitablePos(), landing);
	EXPECT_GT(hero->movementPointsRemaining(), 0);
	EXPECT_EQ(hero->getManaAvailable(), mana - cost);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(client->casts, 1);
	EXPECT_EQ(client->moves, 0);
	EXPECT_EQ(server->transports, 1);
	EXPECT_EQ(server->accepted, std::vector<bool>({true}));
	EXPECT_EQ(dependentCalls, 0);
	EXPECT_FALSE(gateway->nullkiller->isHeroLocked(hero));
	EXPECT_EQ(rngState(), client->completedRng);
	std::set<ObjectInstanceID> budget;
	EXPECT_TRUE(NK2AI::reserveUnavailableRouteReplan(execution, budget));
	path.nodes.pop_back(); // A repeated bad candidate must not repeat transport.
	const auto repeated = executeChain(path);
	EXPECT_FALSE(repeated.success);
	EXPECT_EQ(repeated.unavailableRouteHero, execution.unavailableRouteHero);
	EXPECT_FALSE(NK2AI::reserveUnavailableRouteReplan(repeated, budget));
	EXPECT_EQ(client->casts, 1);
	EXPECT_EQ(client->moves, 0);
	EXPECT_EQ(rngState(), client->completedRng);
	// Recovery preserves a real useful alternative, rather than declaring success.
	path.nodes.front().coord = landing + int3(1, 0, 0);
	path.nodes.front().turns = 0;
	gateway->nullkiller->invalidatePaths();
	const auto alternative = executeChain(path);
	EXPECT_TRUE(alternative.success);
	EXPECT_FALSE(alternative.unavailableRouteHero.has_value());
	EXPECT_EQ(hero->visitablePos(), path.targetTile());
	EXPECT_GT(client->moves, 0);
	EXPECT_EQ(dependentCalls, 0);
}

TEST_F(Nullkiller2_TransportContinuation, nonemptyFutureRouteAfterTransportStillApproachesAndDefers)
{
	ASSERT_NO_FATAL_FAILURE(prepareTransport());
	const auto target = int3(30, 5, 0);
	auto path = transportPath(target);
	const auto execution = executeChain(path);
	EXPECT_TRUE(execution.success);
	EXPECT_FALSE(execution.unavailableRouteHero.has_value());
	EXPECT_EQ(client->casts, 1);
	EXPECT_GT(client->moves, 0);
	EXPECT_EQ(server->transports, 1);
	EXPECT_NE(hero->visitablePos(), landing);
	EXPECT_NE(hero->visitablePos(), target);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(hero));
	CGPath remaining;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(remaining, target));
	ASSERT_FALSE(remaining.nodes.empty());
	EXPECT_GT(remaining.nodes.front().turns, 0);
	EXPECT_EQ(rngState(), client->completedRng);
}

TEST(Nullkiller2_Engine_TaskFailure, initialAndLaterRouteFailuresShareOneReplanBudget)
{
	const ObjectInstanceID hero(7);
	std::set<ObjectInstanceID> budget;
	EXPECT_TRUE(budget.insert(hero).second); // Existing queued-initial-route admission.
	EXPECT_FALSE(NK2AI::reserveUnavailableRouteReplan({false, hero}, budget));
	EXPECT_FALSE(NK2AI::reserveUnavailableRouteReplan({true, ObjectInstanceID(8)}, budget));
	EXPECT_FALSE(NK2AI::reserveUnavailableRouteReplan({false, std::nullopt}, budget));
	EXPECT_TRUE(NK2AI::reserveUnavailableRouteReplan({false, ObjectInstanceID(8)}, budget));
}

TEST_F(Nullkiller2_TransportContinuation, ordinaryFailureRetainsFailureOutcomeWithoutRouteRetry)
{
	ASSERT_NO_FATAL_FAILURE(prepareTransport());
	const auto rng = rngState();
	const auto position = hero->visitablePos();
	const auto movement = hero->movementPointsRemaining();
	const auto mana = hero->getManaAvailable();
	const auto failed = NK2AI::executeTask(*gateway, NK2AI::Goals::taskptr(NK2AI::Goals::Invalid()));
	EXPECT_FALSE(failed.success);
	EXPECT_FALSE(failed.unavailableRouteHero.has_value());
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
	EXPECT_EQ(hero->getManaAvailable(), mana);
	EXPECT_EQ(client->casts, 0);
	EXPECT_EQ(client->moves, 0);
	EXPECT_EQ(rngState(), rng);
}
