/* RouteActorLossTest.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "mock/GameHandlerTestServer.h"
#include "server/battles/FullGameSnapshotTypes.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/TaskExecution.h"
#include "AI/Nullkiller2/Goals/Composition.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/Actions/BattleAction.h"
#include "lib/CPlayerState.h"
#include "lib/CStack.h"
#include "lib/GameLibrary.h"
#include "lib/battle/BattleInfo.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/logging/CLogger.h"
#include "lib/filesystem/ResourcePath.h"
#include "lib/filesystem/NhArtSha256.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/army/CStackInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForClientBattle.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "server/queries/QueriesProcessor.h"
#include <ctime>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
const PlayerColor OWNER(0);
const PlayerColor ENEMY(1);
const auto ENEMY_CONNECTION = static_cast<GameConnectionID>(2);
constexpr int SCENARIO_SEED = 1337;
constexpr std::time_t SCENARIO_START_TIME = 1'700'000'000;
constexpr int AUTHORITATIVE_SEED = 1338;
constexpr int FIXTURE_SEED = 1339;
constexpr int THREAD_DEFAULT_SEED = 1340;

template<class T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

std::string rngHex(const std::vector<std::byte> & bytes)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for(const auto value : bytes)
	{
		const auto byte = std::to_integer<unsigned>(value);
		result += digits[byte >> 4];
		result += digits[byte & 15];
	}
	return result;
}

std::string worldFingerprint(const std::vector<std::byte> & bytes)
{
	return nhart::hex(nhart::sha256(std::span<const ui8>(
		reinterpret_cast<const ui8 *>(bytes.data()), bytes.size())));
}

struct ErrorLog
{
	std::mutex mutex;
	std::vector<std::string> messages;
};

class RouteErrorTarget final : public ILogTarget
{
	std::weak_ptr<ErrorLog> errors;
public:
	explicit RouteErrorTarget(std::weak_ptr<ErrorLog> errors) : errors(std::move(errors)) {}
	void write(const LogRecord & record) override
	{
		if(record.level != ELogLevel::ERROR)
			return;
		if(const auto output = errors.lock())
		{
			std::lock_guard lock(output->mutex);
			output->messages.push_back(record.domain.getName() + ": " + record.message);
		}
	}
};

// Real client ordering on one authoritative state, as in TaskFailureTest's loopback.
class DefeatServer final : public GameHandlerTestServer
{
	std::shared_ptr<CGameState> state;
public:
	NK2AI::AIGateway * gateway = nullptr;
	ObjectInstanceID routeHero;
	std::vector<bool> accepted;
	std::ostringstream replies;
	std::vector<std::string> lifecycle;
	std::optional<BattleResult> result;
	int starts = 0;
	int resultsApplied = 0;
	int ended = 0;
	int ownRemovals = 0;
	bool removedBeforeApply = false;

	explicit DefeatServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(state, OWNER), state(std::move(state)) {}
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return (player == OWNER && connection == GameConnectionID::FIRST_CONNECTION)
			|| (player == ENEMY && connection == ENEMY_CONNECTION);
	}
	void applyPack(CPackForClient & pack) override
	{
		if(auto * start = dynamic_cast<BattleStart *>(&pack); start && gateway)
		{
			const auto & b = *start->info;
			gateway->battleStartBefore(start->battleID, b.getSideArmy(BattleSide::ATTACKER),
				b.getSideArmy(BattleSide::DEFENDER), b.tile,
				b.getSideHero(BattleSide::ATTACKER), b.getSideHero(BattleSide::DEFENDER));
		}
		if(auto * end = dynamic_cast<BattleResult *>(&pack); end && gateway)
		{
			result = *end;
			lifecycle.push_back("battleEnd-before-apply");
			gateway->battleEnd(end->battleID, end, end->queryID);
			// CClient::battleFinished retires the callback after battleEnd, before state apply.
			gateway->cbc->onBattleEnded(end->battleID);
		}
		if(auto * removal = dynamic_cast<RemoveObject *>(&pack); removal && gateway)
		{
			const auto * object = state->getObj(removal->objectID, false);
			if(object && object->getOwner() == OWNER)
			{
				removedBeforeApply = removal->objectID == routeHero && object->ID == Obj::HERO;
				++ownRemovals;
				lifecycle.push_back("objectRemoved-before-apply");
				gateway->objectRemoved(object, removal->initiator);
			}
		}
		GameHandlerTestServer::applyPack(pack);
		forwardApplied(pack);
	}
	void forwardApplied(CPackForClient & pack)
	{
		if(!gateway)
			return;
		if(auto * blocked = dynamic_cast<PlayerBlocked *>(&pack); blocked && blocked->player == OWNER)
			gateway->playerBlocked(blocked->reason, blocked->startOrEnd == PlayerBlocked::BLOCKADE_STARTED);
		if(auto * start = dynamic_cast<BattleStart *>(&pack))
		{
			const auto * b = state->getBattle(start->battleID);
			++starts;
			lifecycle.push_back("battleStart-after-apply");
			gateway->cbc->onBattleStarted(b);
			gateway->battleStart(start->battleID, b->getSideArmy(BattleSide::ATTACKER),
				b->getSideArmy(BattleSide::DEFENDER), b->tile,
				b->getSideHero(BattleSide::ATTACKER), b->getSideHero(BattleSide::DEFENDER),
				BattleSide::ATTACKER, false);
		}
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack))
		{
			gateway->invalidatePaths();
			gateway->heroMoved(*move, true);
		}
		if(dynamic_cast<SetMovePoints *>(&pack) || dynamic_cast<RemoveObject *>(&pack))
			gateway->invalidatePaths();
		if(dynamic_cast<RemoveObject *>(&pack))
			gateway->objectRemovedAfter();
		if(dynamic_cast<BattleResultsApplied *>(&pack))
		{
			++resultsApplied;
			lifecycle.push_back("battleResultsApplied-after-apply");
			gateway->battleResultsApplied();
		}
		if(dynamic_cast<BattleEnded *>(&pack))
		{
			++ended;
			lifecycle.push_back("battleEnded-after-apply");
			gateway->battleEnded();
		}
	}
	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		if(auto * reply = dynamic_cast<PackageApplied *>(&pack))
		{
			accepted.push_back(reply->result);
			replies << "request=" << reply->requestID << " player=" << reply->player.getNum()
				<< " type=" << reply->packType << " accepted=" << reply->result << '\n';
			if(gateway && connection == GameConnectionID::FIRST_CONNECTION)
				gateway->requestRealized(reply);
		}
	}
};

class DefeatClient final : public IClient
{
	CGameHandler & handler;
	DefeatServer & server;
	std::shared_ptr<CGameState> state;
	CRandomGenerator & fixtureRng;
	int sequence = 0;
public:
	NK2AI::AIGateway * gateway = nullptr;
	int moves = 0;
	int battleCommands = 0;
	int shots = 0;
	int postLossMoves = 0;
	std::ostringstream requests;
	std::vector<std::byte> completedWorld;
	std::tuple<std::vector<std::byte>, std::vector<std::byte>, std::vector<std::byte>> completedRng;

	DefeatClient(CGameHandler & handler, DefeatServer & server,
		std::shared_ptr<CGameState> state, CRandomGenerator & fixtureRng)
		: handler(handler), server(server), state(std::move(state)), fixtureRng(fixtureRng) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		const bool moving = dynamic_cast<const MoveHero *>(incoming.get()) != nullptr;
		if(!moving && !dynamic_cast<const MakeAction *>(incoming.get()))
			throw std::runtime_error("Defeat route refuses unrelated commands");
		incoming->player = player;
		incoming->requestID = ++sequence;
		const auto id = incoming->requestID;
		requests << "request=" << id << " player=" << player.getNum();
		if(const auto * move = dynamic_cast<const MoveHero *>(incoming.get()))
			requests << " MoveHero actor=" << move->hid.getNum() << " layer=" << static_cast<int>(move->layer)
				<< " destination=" << (move->path.empty() ? "EMPTY" : move->path.front().toString());
		else if(const auto * battleAction = dynamic_cast<const MakeAction *>(incoming.get()))
			requests << " MakeAction battle=" << battleAction->battleID.getNum()
				<< " action=" << static_cast<int>(battleAction->ba.actionType);
		requests << '\n';
		if(moving)
		{
			++moves;
			if(server.ownRemovals)
				++postLossMoves;
		}
		else
			++battleCommands;
		if(player == OWNER)
			gateway->requestSent(incoming.get(), id);
		handler.handleReceivedPack(player == OWNER ? GameConnectionID::FIRST_CONNECTION : ENEMY_CONNECTION, *incoming);
		if(moving)
		{
			resolveBattle();
			completedWorld = state->saveToMemory();
			completedRng = {serialized(*handler.randomizer), serialized(fixtureRng),
				serialized(CRandomGenerator::getDefault())};
		}
		return id;
	}
	void resolveBattle()
	{
		for(int actions = 0; !state->currentBattles.empty() && actions < 128; ++actions)
		{
			const auto * b = state->currentBattles.front().get();
			const auto & deployment = b->getDeploymentState();
			if(const auto side = deployment.independent ? deployment.activeSide() : b->tacticsSide;
				side != BattleSide::NONE && (deployment.independent || b->tacticDistance > 0))
			{
				submit(b->getBattleID(), b->sideToPlayer(side), BattleAction::makeEndOFTacticPhase(side));
				continue;
			}
			const auto * active = b->battleActiveUnit();
			if(!active)
				throw std::runtime_error("Normal defeat requires an actual active stack");
			auto action = BattleAction::makeDefend(active);
			if(active->unitSide() == BattleSide::DEFENDER)
			{
				const auto targets = b->battleGetStacksIf([](const CStack * s)
					{ return s->alive() && s->unitSide() == BattleSide::ATTACKER; });
				if(targets.size() != 1 || !b->battleCanShoot(active, targets.front()->getPosition()))
					throw std::runtime_error("Normal defeat requires the ordinary ranged target and remaining shots");
				action = BattleAction::makeShotAttack(active, targets.front());
				++shots;
			}
			submit(b->getBattleID(), b->battleGetActionController(active), action);
		}
		if(!state->currentBattles.empty())
			throw std::runtime_error("Normal defeat exceeded the finite 128-action bound");
	}
	void submit(BattleID battle, PlayerColor player, BattleAction action)
	{
		MakeAction request(std::move(action));
		request.battleID = battle;
		const auto before = server.accepted.size();
		sendRequest(request, player, false);
		if(server.accepted.size() != before + 1 || !server.accepted.back())
			throw std::runtime_error("Authoritative normal battle action was rejected");
	}
};

class DependentProbe final : public NK2AI::Goals::ElementarGoal<DependentProbe>
{
	int & calls;
public:
	explicit DependentProbe(int & calls) : calls(calls) {}
	bool operator==(const DependentProbe & other) const override { return &calls == &other.calls; }
	std::string toString() const override { return "post-route dependent probe"; }
	void accept(NK2AI::AIGateway *) override { ++calls; }
};

class RouteActorLossTest : public NullkillerTest
{
protected:
	CGHeroInstance * actor = nullptr;
	CGHeroInstance * survivor = nullptr;
	CGHeroInstance * opponent = nullptr;
	std::unique_ptr<DefeatServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<DefeatClient> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	std::shared_ptr<ErrorLog> errors;
	std::unique_ptr<MapServiceTinyH3M> seededMapService;
	std::vector<std::byte> originalThreadDefault;

	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		originalThreadDefault = serialized(CRandomGenerator::getDefault());
		CRandomGenerator::getDefault().setSeed(THREAD_DEFAULT_SEED);
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(FIXTURE_SEED);
		RecordProperty("InitialFixtureRng", rngHex(serialized(dynamic_cast<CRandomGenerator &>(*getRNG()))));
		RecordProperty("InitialThreadDefaultRng", rngHex(serialized(CRandomGenerator::getDefault())));
		NullkillerTest::SetUp();
	}
	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
		settings.connectedPlayerIDs.clear(); // Both actual AI players auto-confirm normal battle results.
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
		seededMapService.reset();
		CMemorySerializer restore(originalThreadDefault);
		restore.iser & CRandomGenerator::getDefault();
	}
	void startSeededMap(TinyH3M::TinyH3MBuilder builder)
	{
		// Same TinyMap start flow, but its local initializer has no existing seed hook.
		const auto * test = ::testing::UnitTest::GetInstance()->current_test_info();
		auto name = std::string(test->test_suite_name()) + "_" + test->name();
		std::replace(name.begin(), name.end(), '/', '_');
		seededMapService = std::make_unique<MapServiceTinyH3M>(builder.buildAndDump(name), this);
		StartInfo start;
		start.mapname = "tiny";
		start.startTime = SCENARIO_START_TIME;
		RecordProperty("ScenarioStartTime", std::to_string(SCENARIO_START_TIME));
		start.difficulty = static_cast<ui8>(EMapDifficulty::EASY);
		start.mode = EStartMode::NEW_GAME;
		const auto header = seededMapService->loadMapHeader(ResourcePath(start.mapname));
		ASSERT_NE(header, nullptr);
		for(int index = 0; index < static_cast<int>(header->players.size()); ++index)
		{
			const auto & info = header->players[index];
			if(!(info.canHumanPlay || info.canComputerPlay))
				continue;
			auto & player = start.playerInfos[PlayerColor(index)];
			player.color = PlayerColor(index);
			player.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(index));
			player.name = "Player";
			player.castle = info.defaultCastle();
			player.hero = info.defaultHero();
			configurePlayer(player);
		}
		GameRandomizer initializer(*gameState());
		initializer.setSeed(SCENARIO_SEED);
		RecordProperty("InitialScenarioRng", rngHex(serialized(initializer)));
		Load::ProgressAccumulator progress;
		gameState()->init(seededMapService.get(), &start, initializer, progress, false);
		RecordProperty("ScenarioRngAfterInit", rngHex(serialized(initializer)));
		ASSERT_NE(map(), nullptr);
		onMapStarted();
	}
	void prepare()
	{
		const CreatureID genie(CreatureID::decode("core:genie"));
		const CreatureID marksman(CreatureID::decode("core:marksman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("RouteActorLoss").playerActive(OWNER).playerActive(ENEMY)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:astral")), OWNER).heroGarrison({{genie, 1}}).heroSpells({}).heroEquipped({})
			.hero({20, 20, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), OWNER).heroGarrison({{genie, 1}}).heroSpells({}).heroEquipped({})
			.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), ENEMY).heroGarrison({{marksman, 3}}).heroSpells({}).heroEquipped({});
		server = std::make_unique<DefeatServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->randomizer->setSeed(AUTHORITATIVE_SEED);
		RecordProperty("InitialAuthoritativeRng", rngHex(serialized(*handler->randomizer)));
		ASSERT_NO_FATAL_FAILURE(startSeededMap(std::move(builder)));
		for(auto * hero : findAll<CGHeroInstance>())
		{
			if(hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:astral")))
				actor = hero;
			else if(hero->getOwner() == OWNER)
				survivor = hero;
			else
				opponent = hero;
		}
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(survivor, nullptr);
		ASSERT_NE(opponent, nullptr);
		ASSERT_EQ(actor->stacksCount(), 1);
		ASSERT_EQ(actor->getStack(SlotID(0)).getCreatureID(), genie);
		ASSERT_EQ(actor->getStackCount(SlotID(0)), 1);
		ASSERT_EQ(opponent->stacksCount(), 1);
		ASSERT_EQ(opponent->getStack(SlotID(0)).getCreatureID(), marksman);
		ASSERT_EQ(opponent->getStackCount(SlotID(0)), 3);
		ASSERT_FALSE(gameState()->getPlayerState(OWNER)->isHuman());
		ASSERT_FALSE(gameState()->getPlayerState(ENEMY)->isHuman());
		ASSERT_NE(gameState()->getPlayerState(OWNER)->team, gameState()->getPlayerState(ENEMY)->team);
		for(int x = 0; x < map()->width; ++x)
			for(int y = 0; y < map()->height; ++y)
				map()->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
		revealMap(OWNER);
		actor->setMovementPoints(200);
		survivor->setMovementPoints(200);
		gameState()->actingPlayers.insert(OWNER);
		server->routeHero = actor->id;
		client = std::make_unique<DefeatClient>(*handler, *server, gameState(), dynamic_cast<CRandomGenerator &>(*getRNG()));
		gateway = makeGateway(OWNER, client.get());
		server->gateway = client->gateway = gateway.get();
		errors = std::make_shared<ErrorLog>();
		CLogger::getGlobalLogger()->addTarget(std::make_unique<RouteErrorTarget>(errors));
	}
	NK2AI::AIPath generatedPath(CGHeroInstance * hero, int3 destination)
	{
		NK2AI::HeroMap<NK2AI::HeroRole> heroes;
		heroes.emplace(hero, NK2AI::MAIN);
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = settings.useDimensionDoor = false;
		settings.mainTurnDistanceLimit = settings.scoutTurnDistanceLimit = 1;
		gateway->nullkiller->pathfinder->updatePaths(heroes, settings);
		for(const auto & path : gateway->nullkiller->pathfinder->getPathInfo(destination))
			if(path.targetHero == hero && path.turn() == 0)
				return path;
		throw std::runtime_error("Require an actual generated current-day path; do not manufacture an attack node");
	}
	auto rngState()
	{
		return std::tuple(serialized(*handler->randomizer), serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			serialized(CRandomGenerator::getDefault()));
	}
	NK2AI::TaskExecutionResult execute(const NK2AI::AIPath & path, int & dependentCalls)
	{
		NK2AI::Goals::Composition sequence;
		sequence.addNextSequence({NK2AI::Goals::sptr(NK2AI::Goals::ExecuteHeroChain(path)),
			NK2AI::Goals::sptr(DependentProbe(dependentCalls))});
		RecordProperty("BeforeFirstCommandAuthoritativeRng", rngHex(serialized(*handler->randomizer)));
		RecordProperty("BeforeFirstCommandFixtureRng", rngHex(serialized(dynamic_cast<CRandomGenerator &>(*getRNG()))));
		RecordProperty("BeforeFirstCommandThreadDefaultRng", rngHex(serialized(CRandomGenerator::getDefault())));
		RecordProperty("BeforeFirstCommandWorldSha256", worldFingerprint(gameState()->saveToMemory()));
		std::shared_lock lock(CGameState::mutex);
		return NK2AI::executeTask(*gateway, NK2AI::Goals::taskptr(sequence));
	}
	void recordErrors()
	{
		std::ostringstream messages;
		for(const auto & message : errors->messages)
			messages << message << '\n';
		RecordProperty("ActualErrors", messages.str());
		RecordProperty("ActualErrorCount", static_cast<int>(errors->messages.size()));
		RecordProperty("AuthenticatedRequests", client->requests.str());
		RecordProperty("AuthoritativeReplies", server->replies.str());
		std::ostringstream lifecycle;
		for(const auto & event : server->lifecycle)
			lifecycle << event << '\n';
		RecordProperty("BattleLifecycle", lifecycle.str());
	}
};
}

TEST_F(RouteActorLossTest, NormalDefeatAbortsDependentRouteAndPreservesAuthoritativeOutcome)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actorID = actor->id;
	const auto actorType = actor->getHeroTypeID();
	const auto survivorID = survivor->id;
	const auto survivorPos = survivor->visitablePos();
	const auto opponentID = opponent->id;
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	const auto path = generatedPath(actor, opponent->visitablePos());
	ASSERT_TRUE(std::ranges::any_of(path.nodes, [](const auto & node)
		{ return dynamic_cast<const NK2AI::AIPathfinding::BattleAction *>(node.specialAction.get()); }));
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(rngState(), rng);
	int dependentCalls = 0;
	const auto execution = execute(path, dependentCalls);
	gateway->finish();
	recordErrors();
	EXPECT_TRUE(errors->messages.empty()) << "A real normal defeat must not become missing-object/task ERROR noise";
	RecordProperty("TaskSuccess", execution.success ? 1 : 0);
	RecordProperty("DefeatedActorID", actorID.getNum());
	RecordProperty("PostLossMoveRequests", client->postLossMoves);
	EXPECT_FALSE(execution.success);
	EXPECT_TRUE(execution.observedActorRemoval);
	EXPECT_FALSE(execution.unavailableRouteHero);
	EXPECT_EQ(dependentCalls, 0);
	EXPECT_EQ(client->postLossMoves, 0);
	EXPECT_GT(client->moves, 0);
	EXPECT_GT(client->shots, 0);
	EXPECT_GT(client->battleCommands, 0);
	EXPECT_TRUE(std::ranges::all_of(server->accepted, [](bool value) { return value; }));
	EXPECT_EQ(server->starts, 1);
	EXPECT_EQ(server->resultsApplied, 1);
	EXPECT_EQ(server->ended, 1);
	EXPECT_EQ(server->ownRemovals, 1);
	EXPECT_EQ(server->lifecycle, (std::vector<std::string>{"battleStart-after-apply", "battleEnd-before-apply",
		"battleResultsApplied-after-apply", "objectRemoved-before-apply", "battleEnded-after-apply"}));
	EXPECT_TRUE(server->removedBeforeApply);
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(gameState()->getObj(actorID, false), nullptr);
	EXPECT_EQ(map()->tryGetFromHeroPool(actorType), actor);
	EXPECT_EQ(gameState()->getObj(survivorID, false), survivor);
	EXPECT_EQ(survivor->visitablePos(), survivorPos);
	EXPECT_EQ(gameState()->getObj(opponentID, false), opponent);
	EXPECT_GT(opponent->stacksCount(), 0);
	EXPECT_EQ(gameState()->getPlayerState(OWNER)->status, EPlayerStatus::INGAME);
	EXPECT_EQ(gameState()->getPlayerState(ENEMY)->status, EPlayerStatus::INGAME);
	EXPECT_EQ(handler->queries->topQuery(OWNER), nullptr);
	EXPECT_EQ(handler->queries->topQuery(ENEMY), nullptr);
	const auto queries = handler->queries->allQueries();
	EXPECT_TRUE(queries.begin() == queries.end());
	EXPECT_TRUE(gateway->cbc->getActiveBattles().empty());
	EXPECT_EQ(gameState()->saveToMemory(), client->completedWorld);
	EXPECT_EQ(rngState(), client->completedRng);
	ASSERT_TRUE(server->result.has_value());
	EXPECT_EQ(server->result->result, EBattleResult::NORMAL);
	EXPECT_EQ(server->result->winner, BattleSide::DEFENDER);
	EXPECT_EQ(server->result->queryID, QueryID::NONE);
}

TEST_F(RouteActorLossTest, SurvivingOrdinaryRouteStillCompletesItsDependentTask)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = survivor->visitablePos() + int3(1, 0, 0);
	const auto path = generatedPath(survivor, target);
	int dependentCalls = 0;
	const auto execution = execute(path, dependentCalls);
	gateway->finish();
	recordErrors();
	EXPECT_TRUE(execution.success);
	EXPECT_FALSE(execution.observedActorRemoval);
	EXPECT_EQ(dependentCalls, 1);
	EXPECT_EQ(survivor->visitablePos(), target);
	EXPECT_EQ(server->starts, 0);
	EXPECT_EQ(server->ownRemovals, 0);
	EXPECT_EQ(client->battleCommands, 0);
	EXPECT_TRUE(errors->messages.empty());
	EXPECT_TRUE(std::ranges::all_of(server->accepted, [](bool value) { return value; }));
	EXPECT_EQ(gameState()->saveToMemory(), client->completedWorld);
	EXPECT_EQ(rngState(), client->completedRng);
}
