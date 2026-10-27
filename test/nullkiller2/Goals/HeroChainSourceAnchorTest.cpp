/* HeroChainSourceAnchorTest.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "mock/GameHandlerTestServer.h"
#include "server/battles/FullGameSnapshotTypes.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/TaskExecution.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/Actors.h"
#include "lib/callback/CCallback.h"
#include "lib/CPlayerState.h"
#include "lib/callback/IClient.h"
#include "lib/logging/CLogger.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CRewardableObject.h"
#include "lib/mapObjects/MiscObjects.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "server/queries/QueriesProcessor.h"
#include "server/queries/CQuery.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <sstream>
#include <utility>
#include <shared_mutex>
#include <stdexcept>
#include <tuple>

namespace
{
const PlayerColor PLAYER(0);

template<class T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

struct SourceDiagnostics
{
	std::mutex mutex;
	std::string records;
};

class SourceWarningTarget final : public ILogTarget
{
	std::weak_ptr<std::atomic_size_t> count;
	std::string heroName;
	std::weak_ptr<SourceDiagnostics> diagnostics;
public:
	SourceWarningTarget(std::weak_ptr<std::atomic_size_t> count, std::string heroName,
		std::weak_ptr<SourceDiagnostics> diagnostics)
		: count(std::move(count)), heroName(std::move(heroName)), diagnostics(std::move(diagnostics)) {}
	void write(const LogRecord & record) override
	{
		if(record.level == ELogLevel::WARN || record.level == ELogLevel::ERROR)
		{
			if(const auto value = diagnostics.lock())
			{
				std::lock_guard lock(value->mutex);
				value->records += record.domain.getName() + " level=" + std::to_string(record.level)
					+ " " + record.message + "\n";
			}
		}
		if(record.level == ELogLevel::WARN && record.message.starts_with("Why do I want to move hero ")
			&& (heroName.empty() || record.message.find(heroName) != std::string::npos))
		{
			if(const auto value = count.lock())
				++*value;
		}
	}
};

// Same validated loopback ordering as TaskFailureTest's RouteServer/RouteClient.
class SourceRouteServer final : public GameHandlerTestServer
{
public:
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<bool> accepted;
	std::vector<PackageApplied> replies;
	std::vector<PlayerColor> endedPlayers;
	std::vector<ExchangeDialog> pendingExchanges;
	int garrisonUpdates = 0;
	explicit SourceRouteServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(std::move(state), PLAYER) {}
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack);
		if(const auto * ended = dynamic_cast<const PlayerEndsGame *>(&pack))
			endedPlayers.push_back(ended->player);
		if(const auto * exchange = dynamic_cast<const ExchangeDialog *>(&pack))
			pendingExchanges.push_back(*exchange);
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack); move && gateway)
		{
			gateway->invalidatePaths();
			gateway->heroMoved(*move, true);
		}
		else if(dynamic_cast<SetMovePoints *>(&pack) && gateway)
			gateway->invalidatePaths();
		else if(dynamic_cast<CGarrisonOperationPack *>(&pack) && gateway)
		{
			++garrisonUpdates;
			gateway->invalidatePaths();
		}
	}
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * reply = dynamic_cast<PackageApplied *>(&pack); reply && gateway)
		{
			replies.push_back(*reply);
			accepted.push_back(reply->result);
			gateway->requestRealized(reply);
		}
	}
};

class SourceRouteClient final : public IClient
{
	CGameHandler & handler;
	SourceRouteServer & server;
	std::mutex commandMutex;
	int sequence = 0;
public:
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<MoveHero> moves;
	std::ostringstream requests;
	int arrangements = 0;
	int queryReplies = 0;
	int unexpectedCommands = 0;
	std::function<void()> firstRequestProbe;
	SourceRouteClient(CGameHandler & handler, SourceRouteServer & server) : handler(handler), server(server) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		int requestID = 0;
		std::vector<ExchangeDialog> exchanges;
		{
			// The production query worker may answer while the route thread waits.
			// Serialize handler calls exactly as the authoritative command queue does.
			std::lock_guard lock(commandMutex);
			auto incoming = CMemorySerializer::deepCopy(request);
			const auto * move = dynamic_cast<const MoveHero *>(incoming.get());
			if(sequence == 0 && firstRequestProbe)
				firstRequestProbe();
			if(move)
				moves.push_back(*move);
			else if(dynamic_cast<const ArrangeStacks *>(incoming.get()))
				++arrangements;
			else if(dynamic_cast<const QueryReply *>(incoming.get()))
				++queryReplies;
			else
				++unexpectedCommands;
			incoming->player = player;
			incoming->requestID = requestID = ++sequence;
			requests << "request=" << requestID << " player=" << player.getNum();
			if(move)
				requests << " MoveHero hero=" << move->hid.getNum() << " layer=" << static_cast<int>(move->layer)
					<< " destination=" << (move->path.empty() ? "EMPTY" : move->path.front().toString());
			else if(const auto * arrange = dynamic_cast<const ArrangeStacks *>(incoming.get()))
				requests << " ArrangeStacks what=" << static_cast<int>(arrange->what)
					<< " id1=" << arrange->id1.getNum() << " id2=" << arrange->id2.getNum()
					<< " p1=" << arrange->p1.getNum() << " p2=" << arrange->p2.getNum() << " val=" << arrange->val;
			else if(const auto * query = dynamic_cast<const QueryReply *>(incoming.get()))
				requests << " QueryReply qid=" << query->qid.getNum()
					<< " reply=" << (query->reply ? std::to_string(*query->reply) : "NONE");
			else
				requests << " UNEXPECTED";
			if(move)
			{
				if(const auto * actor = gateway->cc->getHero(move->hid))
					requests << " actualPosition=" << actor->visitablePos().toString()
						<< " movement=" << actor->movementPointsRemaining();
			}
			const auto beforeQuery = handler.queries->topQuery(player);
			requests << " beforeQuery=" << (beforeQuery ? beforeQuery->toString() : "NONE")
				<< " acting=" << handler.gameState().actingPlayers.contains(player)
				<< " status=" << static_cast<int>(handler.gameState().getPlayerState(player)->status) << "\n";
			gateway->requestSent(incoming.get(), requestID);
			handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
			const auto afterQuery = handler.queries->topQuery(player);
			requests << "completed=" << requestID << " afterQuery="
				<< (afterQuery ? afterQuery->toString() : "NONE")
				<< " acting=" << handler.gameState().actingPlayers.contains(player)
				<< " status=" << static_cast<int>(handler.gameState().getPlayerState(player)->status) << "\n";
			exchanges = std::exchange(server.pendingExchanges, {});
		}
		// heroExchange adds its query AFTER emitting ExchangeDialog. Deliver
		// after the command and mutex release, so replies cannot outrun registration.
		for(const auto & exchange : exchanges)
			gateway->heroExchangeStarted(exchange.hero1, exchange.hero2, exchange.queryID);
		return requestID;
	}
};

class SourceAction final : public NK2AI::SpecialAction
{
	int & calls;
public:
	explicit SourceAction(int & calls) : calls(calls) {}
	std::string toString() const override { return "source action probe"; }
	void execute(NK2AI::AIGateway *, const CGHeroInstance *) const override { ++calls; }
};

class HeroChainSourceAnchorTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * donor = nullptr;
	CGMine * mine = nullptr;
	std::unique_ptr<SourceRouteServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<SourceRouteClient> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	std::shared_ptr<std::atomic_size_t> warnings;
	std::shared_ptr<SourceDiagnostics> diagnostics;
	void configurePlayer(PlayerSettings & settings) const override
	{
		// Keep army-exchange controls free of random starting equipment.
		settings.bonus = PlayerStartingBonus::GOLD;
	}
	void TearDown() override
	{
		if(gateway)
			gateway->finish();
		if(server)
			server->gateway = nullptr;
		gateway.reset();
		client.reset();
		handler.reset();
		server.reset();
		NullkillerTest::TearDown();
	}
	void prepare(bool onMine = false, bool withDonor = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Hero chain source anchor").playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(0), 1}});
		if(withDonor)
		{
			builder.heroSpells({}).heroEquipped({});
			builder.hero({9, 5, 0}, HeroTypeID(1), PLAYER)
				.heroGarrison({{CreatureID(0), 1}}).heroSpells({}).heroEquipped({});
			// A real living opponent prevents an army transfer from winning a one-player map.
			builder.playerActive(PlayerColor(1)).hero({30, 30, 0}, HeroTypeID(2), PlayerColor(1))
				.heroGarrison({{CreatureID(0), 1}});
		}
		if(onMine)
			builder.mine({15, 15, 0}, MapObjectSubID(0), PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(hero, nullptr);
		if(withDonor)
		{
			donor = findHeroAt({9, 5, 0});
			ASSERT_NE(donor, nullptr);
			ASSERT_NE(donor, hero);
			const auto * opponent = findHeroByOwner(PlayerColor(1));
			ASSERT_NE(opponent, nullptr);
			ASSERT_EQ(opponent->visitablePos(), opponent->convertToVisitablePos(int3(30, 30, 0)));
			ASSERT_EQ(gameState()->getPlayerState(PlayerColor(1))->status, EPlayerStatus::INGAME);
			for(auto * current : {hero, donor})
			{
				ASSERT_TRUE(current->usesNewHorizonsMovement());
				ASSERT_TRUE(current->getSpellsInSpellbook().empty());
				ASSERT_TRUE(current->artifactsInBackpack.empty());
				for(const auto & artifact : current->artifactsWorn)
				{
					if(artifact.second.getArt())
					{
						ASSERT_EQ(artifact.first, ArtifactPosition::MACH4);
					}
				}
				ASSERT_FALSE(current->hasActivePerk("new-horizons:learning", "new-horizons:learning.mentor"));
				ASSERT_FALSE(current->hasActivePerk("new-horizons:learning", "new-horizons:learning.masterTeacher"));
				const auto capacity = current->getLeadershipSlotCapacity(CreatureID(0));
				ASSERT_TRUE(capacity);
				ASSERT_GE(capacity->maximum, 10);
				current->clearSlots();
				for(int slot = 0; slot < 3; ++slot)
					ASSERT_TRUE(current->setCreature(SlotID(slot), CreatureID(0), 10));
				ASSERT_GE(current->getArmyStrength(), 1000u);
				current->setMovementPoints(200);
			}
		}
		for(int x = 0; x < map()->width; ++x)
			for(int y = 0; y < map()->height; ++y)
				map()->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
		if(onMine)
		{
			mine = findFirst<CGMine>();
			ASSERT_NE(mine, nullptr);
			mine->clearSlots();
			ChangeObjPos relocate;
			relocate.objid = hero->id;
			relocate.nPos = mine->visitablePos();
			relocate.initiator = PLAYER;
			gameState()->apply(relocate);
			ASSERT_EQ(hero->visitablePos(), mine->visitablePos());
		}
		revealMap(PLAYER);
		hero->setMovementPoints(200);
		gameState()->actingPlayers.insert(PLAYER);
		server = std::make_unique<SourceRouteServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		client = std::make_unique<SourceRouteClient>(*handler, *server);
		gateway = makeGateway(PLAYER, client.get());
		server->gateway = client->gateway = gateway.get();
		ASSERT_FALSE(gateway->nullkiller->isObjectGraphAllowed()) << "Characterize native origin execution without graph coalescing";
		warnings = std::make_shared<std::atomic_size_t>(0);
		diagnostics = std::make_shared<SourceDiagnostics>();
		CLogger::getGlobalLogger()->addTarget(std::make_unique<SourceWarningTarget>(
			warnings, withDonor ? hero->getNameTextID() : std::string{}, diagnostics));
	}
	NK2AI::AIPath generatedPath(const int3 & target)
	{
		NK2AI::HeroMap<NK2AI::HeroRole> heroes;
		heroes.emplace(hero, NK2AI::MAIN);
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		settings.useDimensionDoor = false;
		settings.mainTurnDistanceLimit = settings.scoutTurnDistanceLimit = 1;
		gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
		const auto paths = gateway->nullkiller->pathfinder->getPathInfo(target);
		const auto found = std::ranges::find_if(paths, [this](const NK2AI::AIPath & path)
			{ return path.targetHero == hero && path.turn() == 0; });
		if(found == paths.end())
			throw std::runtime_error("Fixture requires an actual current-day generated route");
		return *found;
	}
	NK2AI::AIPath stationaryIntent()
	{
		NK2AI::AIPath path;
		path.targetHero = hero;
		path.heroArmy = hero;
		path.chainMask = 1;
		path.exchangeCount = 1;
		path.targetObjectDanger = path.armyLoss = path.targetObjectArmyLoss = 0;
		NK2AI::AIPathNodeInfo node{};
		node.coord = hero->visitablePos();
		node.targetHero = hero;
		node.chainMask = 1;
		node.parentIndex = -1;
		node.layer = EPathfindingLayer::LAND;
		path.nodes.push_back(node);
		return path;
	}
	std::optional<std::pair<NK2AI::AIPath, const NK2AI::AIPathNode *>> waitingReceiverPath(const int3 & target)
	{
		gateway->nullkiller->heroManager->update();
		NK2AI::HeroMap<NK2AI::HeroRole> heroes{{hero, NK2AI::MAIN}, {donor, NK2AI::MAIN}};
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = true;
		settings.useDimensionDoor = false;
		settings.mainTurnDistanceLimit = settings.scoutTurnDistanceLimit = 1;
		gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
		std::vector<NK2AI::AIPathSummary> summaries;
		gateway->nullkiller->pathfinder->calculatePathSummaries(summaries, target);
		for(const auto & summary : summaries)
		{
			if(summary.targetHero != hero || summary.exchangeCount != 2)
				continue;
			for(auto * node = summary.node; node; node = static_cast<const NK2AI::AIPathNode *>(node->theNodeBefore))
			{
				if(node->coord != hero->visitablePos() || node->actor->hero != hero
					|| node->specialAction || node->theNodeBefore || !node->chainOther
					|| node->chainOther->actor->hero != donor || node->turns != 0
					|| node->action != EPathNodeAction::NORMAL || node->layer != EPathfindingLayer::LAND)
					continue;
				NK2AI::AIPath path;
				if(gateway->nullkiller->pathfinder->calculatePathInfo(path, summary))
					return std::pair{std::move(path), node};
			}
		}
		return std::nullopt;
	}
	CRewardableObject * movementEndingPond()
	{
		auto object = handler->createNewObject(hero->anchorPos() + int3(2, 3, 0), Obj::SWAN_POND, MapObjectSubID(0));
		if(!object)
			return nullptr;
		handler->newObject(object, PLAYER);
		gateway->invalidatePaths();
		return dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(object->id));
	}
	NK2AI::AIPath meetingWithDependentAction(const int3 & target, int & calls, uint8_t turns = 0)
	{
		auto path = stationaryIntent();
		path.exchangeCount = 2;
		path.nodes.front().coord = donor->visitablePos();
		path.nodes.front().targetHero = donor;
		path.nodes.front().specialAction = std::make_shared<SourceAction>(calls);
		NK2AI::AIPathNodeInfo meeting{};
		meeting.coord = target;
		meeting.targetHero = hero;
		meeting.chainMask = path.chainMask;
		meeting.parentIndex = 0;
		meeting.layer = EPathfindingLayer::LAND;
		meeting.turns = turns;
		path.nodes.push_back(meeting);
		return path;
	}
	NK2AI::TaskExecutionResult execute(const NK2AI::AIPath & path)
	{
		std::shared_lock lock(CGameState::mutex);
		return NK2AI::executeTask(*gateway, NK2AI::Goals::taskptr(NK2AI::Goals::ExecuteHeroChain(path)));
	}
	auto rngState()
	{
		return std::tuple(serialized(*handler->randomizer),
			serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), serialized(CRandomGenerator::getDefault()));
	}
};
}

TEST_F(HeroChainSourceAnchorTest, CompressedOrdinaryRouteExecutesWithoutOriginWarningOrRequest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto origin = hero->visitablePos();
	const auto target = origin + int3(2, 0, 0);
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	const auto path = generatedPath(target);
	ASSERT_EQ(path.nodes.size(), 1u);
	ASSERT_EQ(path.nodes.back().coord, target);
	ASSERT_FALSE(path.nodes.back().specialAction);
	ASSERT_EQ(path.targetTile(), target);
	ASSERT_EQ(gateway->cc->getVisitableObjs(origin).size(), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
	EXPECT_TRUE(execute(path).success);
	EXPECT_EQ(warnings->load(), 0u);
	ASSERT_FALSE(client->moves.empty());
	for(const auto & move : client->moves)
	{
		ASSERT_FALSE(move.path.empty());
		EXPECT_NE(move.path.front(), hero->convertFromVisitablePos(origin));
	}
	EXPECT_EQ(hero->visitablePos(), target);
	EXPECT_LT(hero->movementPointsRemaining(), 200);
	EXPECT_TRUE(std::ranges::all_of(server->accepted, [](bool accepted) { return accepted; }));
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, NonstructuralSameTileIntentRetainsDiagnosticAndNoPacket)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	EXPECT_TRUE(execute(stationaryIntent()).success);
	EXPECT_EQ(warnings->load(), 1u);
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, CurrentTileObjectVisitRetainsValidatedRequestAndCapture)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto origin = hero->visitablePos();
	ASSERT_EQ(mine->getOwner(), PlayerColor::NEUTRAL);
	ASSERT_EQ(gateway->cc->getVisitableObjs(origin).size(), 2u);
	const auto rng = rngState();
	EXPECT_TRUE(execute(stationaryIntent()).success);
	ASSERT_EQ(client->moves.size(), 1u);
	ASSERT_EQ(client->moves.front().path.size(), 1u);
	EXPECT_EQ(client->moves.front().path.front(), hero->anchorPos());
	EXPECT_EQ(mine->getOwner(), PLAYER);
	EXPECT_EQ(hero->visitablePos(), origin);
	EXPECT_EQ(warnings->load(), 0u);
	EXPECT_EQ(server->accepted, std::vector<bool>({true}));
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, CurrentTileSpecialActionExecutesExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int calls = 0;
	auto path = stationaryIntent();
	path.nodes.front().specialAction = std::make_shared<SourceAction>(calls);
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	EXPECT_TRUE(execute(path).success);
	EXPECT_EQ(calls, 1);
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(warnings->load(), 0u);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, GeneratedWaitingReceiverCompletesDonorExchangeWithoutRedundantReadinessWarning)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_TRUE(gameState()->actingPlayers.contains(PLAYER));
	ASSERT_EQ(gameState()->getPlayerState(PLAYER)->status, EPlayerStatus::INGAME);
	ASSERT_NE(gameState()->getPlayerState(PLAYER)->team, gameState()->getPlayerState(PlayerColor(1))->team);
	const auto origin = hero->visitablePos();
	const auto donorOrigin = donor->visitablePos();
	const auto target = origin + int3(-2, 0, 0);
	ASSERT_NE(origin, donorOrigin);
	ASSERT_EQ(gateway->cc->getVisitableObjs(origin).size(), 1u);
	const auto receiverStrength = hero->getArmyStrength();
	const auto donorMovement = donor->movementPointsRemaining();
	const auto totalCreatures = [this]()
	{
		int count = 0;
		for(const auto * current : {hero, donor})
			for(const auto & slot : current->Slots())
				count += slot.second->getCount();
		return count;
	};
	const int units = totalCreatures();
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	const auto generated = waitingReceiverPath(target);
	ASSERT_TRUE(generated.has_value()) << "Require a real native waiting-carrier exchange, not a fabricated source chain";
	const auto & path = generated->first;
	const auto * native = generated->second;
	ASSERT_EQ(path.targetHero, hero);
	ASSERT_GT(path.exchangeCount, 1);
	ASSERT_EQ(path.targetTile(), target);
	ASSERT_EQ(native->coord, origin);
	ASSERT_EQ(native->actor->hero, hero);
	ASSERT_NE(native->actor->carrierParent, nullptr);
	ASSERT_EQ(native->actor->carrierParent->hero, hero);
	ASSERT_EQ(native->actor->carrierParent->initialPosition, origin);
	ASSERT_NE(native->actor->otherParent, nullptr);
	ASSERT_EQ(native->actor->otherParent->hero, donor);
	ASSERT_EQ(native->actor->otherParent->initialPosition, donorOrigin);
	ASSERT_EQ(native->action, EPathNodeAction::NORMAL);
	ASSERT_EQ(native->layer, EPathfindingLayer::LAND);
	ASSERT_EQ(native->theNodeBefore, nullptr);
	ASSERT_FALSE(native->specialAction);
	ASSERT_NE(native->chainOther, nullptr);
	ASSERT_EQ(native->chainOther->actor->hero, donor);
	ASSERT_EQ(native->chainOther->coord, origin);
	ASSERT_GT(native->chainOther->getCost(), 0.f);
	bool hasReceiverReadiness = false;
	for(size_t index = 0; index < path.nodes.size(); ++index)
	{
		const auto & node = path.nodes[index];
		if(node.targetHero != hero || node.coord != origin || node.specialAction
			|| node.parentIndex < 0 || static_cast<size_t>(node.parentIndex) >= index)
			continue;
		const auto & dependency = path.nodes[node.parentIndex];
		if(dependency.targetHero == donor && dependency.coord == origin && dependency.turns == 0)
		{
			EXPECT_EQ(node.exchangeReadinessDonor, donor);
			EXPECT_TRUE(node.isExchangeReadinessFor(hero, dependency));
			hasReceiverReadiness = true;
		}
	}
	ASSERT_TRUE(hasReceiverReadiness) << path.toString();
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
	std::optional<size_t> warningsBeforeFirstRequest;
	client->firstRequestProbe = [&]() { warningsBeforeFirstRequest = warnings->load(); };
	const auto execution = execute(path);
	EXPECT_TRUE(execution.success);
	gateway->finish(); // Drain owned production query work before inspecting shared receipts.
	RecordProperty("RouteUnavailableHero", execution.unavailableRouteHero
		? std::to_string(execution.unavailableRouteHero->getNum()) : "NONE");
	RecordProperty("AuthoritativeRequests", client->requests.str());
	std::ostringstream replies;
	for(const auto & reply : server->replies)
		replies << "request=" << reply.requestID << " packType=" << reply.packType
			<< " player=" << reply.player.getNum() << " accepted=" << reply.result << "\n";
	RecordProperty("AuthoritativeReplies", replies.str());
	std::ostringstream endedPlayers;
	for(const auto player : server->endedPlayers)
		endedPlayers << player.getNum() << " ";
	RecordProperty("AuthoritativeEndedPlayers", endedPlayers.str());
	{
		std::lock_guard lock(diagnostics->mutex);
		RecordProperty("StructuredWarningsAndErrors", diagnostics->records);
	}
	ASSERT_TRUE(warningsBeforeFirstRequest.has_value());
	RecordProperty("ReadinessWarningsBeforeFirstCommand", static_cast<int>(*warningsBeforeFirstRequest));
	RecordProperty("ReadinessWarningsAfterRoute", static_cast<int>(warnings->load()));
	EXPECT_EQ(warnings->load(), 0u); // A receiver waiting for its donor must not emit a redundant movement warning.
	EXPECT_GT(client->arrangements, 0);
	EXPECT_GT(server->garrisonUpdates, 0);
	EXPECT_EQ(client->queryReplies, 1);
	EXPECT_EQ(client->unexpectedCommands, 0);
	bool movedReceiver = false;
	bool movedDonor = false;
	for(const auto & move : client->moves)
	{
		ASSERT_FALSE(move.path.empty());
		if(move.hid == hero->id)
		{
			movedReceiver = true;
			EXPECT_NE(move.path.front(), hero->convertFromVisitablePos(origin));
		}
		if(move.hid == donor->id)
			movedDonor = true;
	}
	EXPECT_TRUE(movedReceiver);
	EXPECT_TRUE(movedDonor);
	EXPECT_EQ(hero->visitablePos(), target);
	EXPECT_NE(donor->visitablePos(), donorOrigin);
	EXPECT_LT(donor->movementPointsRemaining(), donorMovement);
	EXPECT_GT(hero->getArmyStrength(), receiverStrength);
	EXPECT_GT(donor->stacksCount(), 0);
	EXPECT_EQ(totalCreatures(), units);
	EXPECT_EQ(server->accepted.size(), client->moves.size() + client->arrangements + client->queryReplies);
	EXPECT_TRUE(std::ranges::all_of(server->accepted, [](bool accepted) { return accepted; }));
	EXPECT_TRUE(server->pendingExchanges.empty());
	EXPECT_TRUE(server->endedPlayers.empty());
	EXPECT_TRUE(gameState()->actingPlayers.contains(PLAYER));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->status, EPlayerStatus::INGAME);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(1))->status, EPlayerStatus::INGAME);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, CopiedWaitingReceiverRetainsActorAndCurrentDependencyChecks)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto generated = waitingReceiverPath(hero->visitablePos() + int3(-2, 0, 0));
	ASSERT_TRUE(generated);
	const auto & path = generated->first;
	const auto found = std::ranges::find_if(path.nodes, [](const auto & node)
		{ return node.exchangeReadinessDonor != nullptr; });
	ASSERT_NE(found, path.nodes.end());
	ASSERT_GE(found->parentIndex, 0);
	const auto dependency = path.nodes.at(found->parentIndex);
	const auto copied = *found;
	EXPECT_TRUE(copied.isExchangeReadinessFor(hero, dependency));
	EXPECT_FALSE(copied.isExchangeReadinessFor(donor, dependency));
	auto changed = dependency;
	changed.targetHero = hero;
	EXPECT_FALSE(copied.isExchangeReadinessFor(hero, changed));
	changed = dependency;
	changed.coord += int3(1, 0, 0);
	EXPECT_FALSE(copied.isExchangeReadinessFor(hero, changed));
	changed = dependency;
	changed.actionIsBlocked = true;
	EXPECT_FALSE(copied.isExchangeReadinessFor(hero, changed));
	int calls = 0;
	changed = dependency;
	changed.specialAction = std::make_shared<SourceAction>(calls);
	EXPECT_FALSE(copied.isExchangeReadinessFor(hero, changed));
	changed = copied;
	changed.specialAction = std::make_shared<SourceAction>(calls);
	EXPECT_FALSE(changed.isExchangeReadinessFor(hero, dependency));
	changed = copied;
	changed.actionIsBlocked = true;
	EXPECT_FALSE(changed.isExchangeReadinessFor(hero, dependency));
	NK2AI::AIPathNodeInfo synthetic{};
	EXPECT_EQ(synthetic.exchangeReadinessDonor, nullptr);
}

TEST_F(HeroChainSourceAnchorTest, SlicedStandaloneReadinessRetainsDiagnosticAndNoPacket)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto generated = waitingReceiverPath(hero->visitablePos() + int3(-2, 0, 0));
	ASSERT_TRUE(generated);
	const auto found = std::ranges::find_if(generated->first.nodes, [](const auto & node)
		{ return node.exchangeReadinessDonor != nullptr; });
	ASSERT_NE(found, generated->first.nodes.end());
	auto sliced = stationaryIntent();
	sliced.nodes.front() = *found;
	sliced.nodes.front().parentIndex = -1; // Current copied path has no donor continuation.
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	EXPECT_TRUE(execute(sliced).success);
	EXPECT_EQ(warnings->load(), 1u);
	EXPECT_TRUE(client->moves.empty());
	EXPECT_TRUE(server->accepted.empty());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, NativeVisitBattleTransportAndSpecialEntriesRemainActions)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto generated = waitingReceiverPath(hero->visitablePos() + int3(-2, 0, 0));
	ASSERT_TRUE(generated);
	auto original = *generated->second;
	ASSERT_EQ(NK2AI::exportAIPathNode(original, 0, gateway->nullkiller.get()).exchangeReadinessDonor, donor);
	for(const auto action : {EPathNodeAction::VISIT, EPathNodeAction::BLOCKING_VISIT,
		EPathNodeAction::BATTLE, EPathNodeAction::TELEPORT_NORMAL,
		EPathNodeAction::TELEPORT_BLOCKING_VISIT, EPathNodeAction::TELEPORT_BATTLE,
		EPathNodeAction::EMBARK, EPathNodeAction::DISEMBARK})
	{
		auto candidate = original;
		candidate.action = action;
		EXPECT_EQ(NK2AI::exportAIPathNode(candidate, 0, gateway->nullkiller.get()).exchangeReadinessDonor, nullptr);
	}
	int calls = 0;
	auto candidate = original;
	candidate.specialAction = std::make_shared<SourceAction>(calls);
	EXPECT_EQ(NK2AI::exportAIPathNode(candidate, 0, gateway->nullkiller.get()).exchangeReadinessDonor, nullptr);
	candidate = original;
	candidate.chainOther = nullptr;
	EXPECT_EQ(NK2AI::exportAIPathNode(candidate, 0, gateway->nullkiller.get()).exchangeReadinessDonor, nullptr);
	candidate = original;
	candidate.theNodeBefore = &original;
	EXPECT_EQ(NK2AI::exportAIPathNode(candidate, 0, gateway->nullkiller.get()).exchangeReadinessDonor, nullptr);
}

TEST_F(HeroChainSourceAnchorTest, NativeSwanPondExhaustionDefersStaleSameDayMeetingAndDependentAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	hero->setMovementPoints(hero->movementPointsLimit());
	auto * pond = movementEndingPond();
	ASSERT_NE(pond, nullptr);
	const auto rewards = pond->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
	ASSERT_EQ(rewards.size(), 1u);
	const auto & reward = pond->configuration.info.at(rewards.front()).reward;
	ASSERT_EQ(reward.movePercentage, 0);
	ASSERT_EQ(reward.movePoints, 0);
	const auto pondTile = pond->visitablePos();
	const auto meetingTile = donor->visitablePos();
	ASSERT_NE(pondTile, hero->visitablePos());
	ASSERT_NE(pondTile, meetingTile);
	CGPath approach;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(hero)->getPath(approach, pondTile));
	ASSERT_FALSE(approach.nodes.empty());
	ASSERT_EQ(approach.nodes.front().turns, 0);
	ASSERT_GT(approach.nodes.front().moveRemains, 0);
	int dependentCalls = 0;
	auto path = meetingWithDependentAction(meetingTile, dependentCalls);
	NK2AI::AIPathNodeInfo visit{};
	visit.coord = pondTile;
	visit.targetHero = hero;
	visit.chainMask = path.chainMask;
	visit.parentIndex = 1;
	visit.layer = EPathfindingLayer::LAND;
	path.nodes.push_back(visit); // Stale executor input, not a claim about native chain generation.
	const auto donorBefore = std::tuple(donor->visitablePos(), donor->movementPointsRemaining(),
		donor->getManaAvailable(), donor->getArmyStrength());
	const auto resources = gateway->cc->getResourceAmount();
	const auto luck = hero->valOfBonuses(BonusType::LUCK);
	ASSERT_FALSE(gateway->nullkiller->isHeroLocked(hero));
	ASSERT_FALSE(gateway->nullkiller->isHeroLocked(donor));
	const auto rng = rngState();
	EXPECT_TRUE(execute(path).success); // Existing future-node outcome: deferred, not physically completed.
	EXPECT_EQ(hero->visitablePos(), pondTile);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_EQ(hero->valOfBonuses(BonusType::LUCK), luck + 2);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(hero));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::HERO_CHAIN);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(donor));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(donor), NK2AI::HeroLockedReason::HERO_CHAIN);
	EXPECT_EQ(dependentCalls, 0);
	EXPECT_NE(hero->visitablePos(), meetingTile);
	ASSERT_FALSE(client->moves.empty());
	for(const auto & move : client->moves)
	{
		EXPECT_EQ(move.hid, hero->id);
		EXPECT_FALSE(vstd::contains(move.path, hero->convertFromVisitablePos(meetingTile)));
	}
	EXPECT_TRUE(std::ranges::all_of(server->accepted, [](bool accepted) { return accepted; }));
	EXPECT_EQ(client->arrangements, 0);
	EXPECT_EQ(client->queryReplies, 0);
	EXPECT_EQ(client->unexpectedCommands, 0);
	EXPECT_TRUE(server->pendingExchanges.empty());
	EXPECT_FALSE(handler->queries->topQuery(PLAYER));
	EXPECT_TRUE(server->endedPlayers.empty());
	EXPECT_EQ(std::tuple(donor->visitablePos(), donor->movementPointsRemaining(),
		donor->getManaAvailable(), donor->getArmyStrength()), donorBefore);
	EXPECT_EQ(gateway->cc->getResourceAmount(), resources);
	EXPECT_EQ(rngState(), rng);
	std::lock_guard lock(diagnostics->mutex);
	EXPECT_EQ(diagnostics->records.find("Unable to complete chain."), std::string::npos);
	RecordProperty("StructuredWarningsAndErrors", diagnostics->records);
	RecordProperty("AcceptedMovementRequests", client->requests.str());
}

TEST_F(HeroChainSourceAnchorTest, ExhaustedAlreadyReachedNodeStillCompletesWithoutLockOrRequest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hero->setMovementPoints(0);
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	EXPECT_TRUE(execute(stationaryIntent()).success);
	EXPECT_FALSE(gateway->nullkiller->isHeroLocked(hero));
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(HeroChainSourceAnchorTest, ExhaustedFutureNodeRetainsParentIndexDeferral)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	hero->setMovementPoints(0);
	int dependentCalls = 0;
	const auto path = meetingWithDependentAction(donor->visitablePos(), dependentCalls, 1);
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	EXPECT_TRUE(execute(path).success);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(hero));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(hero), NK2AI::HeroLockedReason::HERO_CHAIN);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(donor));
	EXPECT_EQ(gateway->nullkiller->getHeroLockedReason(donor), NK2AI::HeroLockedReason::HERO_CHAIN);
	EXPECT_EQ(dependentCalls, 0);
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
}
