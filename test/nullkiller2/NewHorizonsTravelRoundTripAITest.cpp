/*
 * NewHorizonsTravelRoundTripAITest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../server/battles/BattleTestFixture.h"
#include "AI/Nullkiller2/AIGateway.h"
#include "lib/GameLibrary.h"
#include "lib/VCMIDirs.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/callback/IClient.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/pathfinder/NewHorizonsTravelSafety.h"
#include "lib/pathfinder/TurnInfo.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/NewHorizonsMagic.h"
#include "server/CGameHandler.h"
#include "server/processors/TurnOrderProcessor.h"
#include "server/queries/QueriesProcessor.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <shared_mutex>
#include <string_view>

namespace
{
namespace fs = std::filesystem;
constexpr PlayerColor player(0);
constexpr PlayerColor opponent(1);
constexpr auto enabledVariable = "NH_TEST_WATER_WALK_ROUNDTRIP";
constexpr auto pendingStem = "water-walk-mid-crossing-roundtrip";

bool containsPath(const fs::path & parent, const fs::path & child)
{
	return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first == parent.end();
}

fs::path checkedDirectory(const char * variable)
{
	const char * value = std::getenv(variable);
	if(!value || !*value)
		throw std::runtime_error("Travel roundtrip requires explicit private directory bindings");
	const fs::path path(value);
	if(!path.is_absolute() || !fs::is_directory(path) || path.lexically_normal() != path)
		throw std::runtime_error("Travel roundtrip requires existing absolute normalized directories");
	for(auto ancestor = path; !ancestor.empty(); ancestor = ancestor.parent_path())
	{
		if(fs::is_symlink(fs::symlink_status(ancestor)))
			throw std::runtime_error("Travel roundtrip rejects symlink ancestors");
		if(ancestor == ancestor.root_path())
			break;
	}
	return fs::canonical(path);
}

fs::path checkedSaveRoot()
{
	const auto root = checkedDirectory("NH_TEST_WATER_WALK_ROUNDTRIP_PRIVATE_ROOT");
	const auto save = checkedDirectory("NH_TEST_WATER_WALK_ROUNDTRIP_SAVE_ROOT");
	const auto source = checkedDirectory("NH_TEST_WATER_WALK_ROUNDTRIP_SOURCE_ROOT");
	if(!fs::is_regular_file(source / "AGENTS.md") || !fs::is_regular_file(source / "CMakeLists.txt")
		|| save == root || !containsPath(root, save)
		|| save != fs::canonical(fs::path(VCMIDirs::get().userSavePath().string())))
		throw std::runtime_error("Travel roundtrip requires its isolated save mount and source binding");
	std::vector<fs::path> protectedRoots{source, fs::current_path(), fs::path(VCMIDirs::get().binaryPath().string())};
	for(const auto & path : VCMIDirs::get().dataPaths())
		protectedRoots.emplace_back(path.string());
	for(const auto & path : protectedRoots)
	{
		const auto canonical = fs::weakly_canonical(path);
		if(containsPath(root, canonical) || containsPath(canonical, root))
			throw std::runtime_error("Travel roundtrip private output overlaps source, binary or data");
	}
	if(fs::symlink_status(save / (std::string(pendingStem) + ".vsgm1")).type() != fs::file_type::not_found)
		throw std::runtime_error("Travel roundtrip refuses an existing output");
	const auto cache = fs::weakly_canonical(fs::path(VCMIDirs::get().userCachePath().string()) / "testMaps");
	const auto dump = cache / "NewHorizonsTravelRoundTripAITest_PaidWaterWalkRestoredMidCrossingLandsThenRenewsOnlyNextDay.h3m";
	if(!containsPath(root, cache) || fs::symlink_status(dump).type() != fs::file_type::not_found)
		throw std::runtime_error("Travel roundtrip requires a fresh isolated map cache");
	return save;
}

class RoundTripServer final : public RecordingGameServer
{
public:
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<bool> acknowledgements;
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * response = dynamic_cast<PackageApplied *>(&pack))
		{
			acknowledgements.push_back(response->result);
			if(gateway)
				gateway->requestRealized(response);
		}
	}
};

class RoundTripClient final : public IClient
{
	CGameHandler & handler;
	int sequence = 0;
public:
	explicit RoundTripClient(CGameHandler & handler) : handler(handler) {}
	std::vector<MoveHero> moves;
	std::vector<CastAdvSpell> casts;
	int endTurns = 0;
	std::function<void()> beforeEndTurn;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor owner, bool waits) override
	{
		EXPECT_TRUE(waits);
		if(const auto * move = dynamic_cast<const MoveHero *>(&request))
			moves.push_back(*move);
		else if(const auto * cast = dynamic_cast<const CastAdvSpell *>(&request))
			casts.push_back(*cast);
		else if(dynamic_cast<const EndTurn *>(&request))
		{
			++endTurns;
			if(beforeEndTurn)
				beforeEndTurn();
		}
		else
			throw std::runtime_error("Travel roundtrip submitted an unsupported request");
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = owner;
		incoming->requestID = ++sequence;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return sequence;
	}
};

class NewHorizonsTravelRoundTripAITest : public TinyMapGameTest
{
protected:
	RoundTripServer server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<RoundTripClient> client;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	ObjectInstanceID heroID;
	Services * gameServices() override { return LIBRARY; }
	CGHeroInstance * hero() { return dynamic_cast<CGHeroInstance *>(handler->gs->getMap().getObject(heroID)); }
	void disconnect()
	{
		server.gateway = nullptr;
		gateway.reset();
		callback.reset();
		client.reset();
	}
	void TearDown() override
	{
		disconnect();
		handler.reset();
		server.gameState.reset();
		TinyMapGameTest::TearDown();
	}
	void connect()
	{
		client = std::make_unique<RoundTripClient>(*handler);
		callback = std::make_shared<CCallback>(handler->gs, player, client.get());
		gateway = std::make_unique<NK2AI::AIGateway>();
		gateway->initGameInterface(std::shared_ptr<Environment>(), callback);
		gateway->status.startedTurn();
		server.gateway = gateway.get();
	}
	template<typename T> static auto bytes(T & value)
	{
		CMemorySerializer serialized;
		serialized.oser & value;
		return serialized.extractBuffer();
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(player).playerActive(opponent)
			.hero({8, 8, 0}, HeroTypeID(0), player).heroPrimary(10, 10, 10, 100)
			.heroSpells({SpellID::WATER_WALK})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.hero({30, 30, 0}, HeroTypeID(1), opponent);
		startWithMap(std::move(builder), EMapDifficulty::NORMAL);
		server.gameState = gameState();
		handler = std::make_unique<CGameHandler>(server, gameState());
		const auto * startingHero = findHeroByOwner(player);
		ASSERT_NE(startingHero, nullptr);
		heroID = startingHero->id;
		for(const auto & owner : gameState()->players)
			handler->turnOrder->addPlayer(owner.first);
		handler->start(false);
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(player));
		ASSERT_TRUE(newHorizonsMagic::requiresWaterWalkLegalDayEnd(hero()->getMagicRules()));
		ASSERT_TRUE(newHorizonsMagic::adventureSpellRulesActive(hero()->getMagicRules()));
		ASSERT_TRUE(hero()->usesNewHorizonsMovement());
		revealMap(player);
		connect();
	}
	void cast()
	{
		std::shared_lock lock(CGameState::mutex);
		callback->castSpell(hero(), SpellID(SpellID::WATER_WALK));
	}
	void moveTo(const int3 & destination)
	{
		std::shared_lock lock(CGameState::mutex);
		callback->moveHero(hero(), hero()->convertFromVisitablePos(destination), true, EPathfindingLayer::WATER);
	}
	void roundTrip(const fs::path & root)
	{
		const auto day = handler->gameState().day;
		const auto rules = hero()->getMagicRules();
		const auto layer = hero()->getProtectedAdventureFlightLayer();
		const auto rng = bytes(*handler->randomizer);
		const auto fixtureRng = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto turnOrder = bytes(*handler->turnOrder);
		const auto filename = std::string("Saves/") + pendingStem;
		disconnect();
		handler->save(filename, PlayerColor::NEUTRAL);
		const auto output = root / (std::string(pendingStem) + ".vsgm1");
		ASSERT_TRUE(fs::is_regular_file(output));
		ASSERT_GT(fs::file_size(output), 0u);
		ASSERT_LT(fs::file_size(output), 64u * 1024u * 1024u);
		StartInfo info = *handler->gameState().getStartInfo();
		info.mode = EStartMode::LOAD_GAME;
		info.mapname = filename;
		handler->load(info);
		server.gameState = handler->gs;
		ASSERT_NE(handler->gs, gameState());
		ASSERT_NE(hero(), nullptr);
		ASSERT_EQ(handler->gameState().day, day);
		ASSERT_EQ(hero()->getMagicRules(), rules);
		ASSERT_EQ(hero()->getProtectedAdventureFlightLayer(), layer);
		ASSERT_EQ(bytes(*handler->randomizer), rng);
		ASSERT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
		ASSERT_EQ(bytes(*handler->turnOrder), turnOrder);
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(player));
		ASSERT_FALSE(handler->queries->topQuery(player));
		connect();
	}
	void expireAndRenew(int day, const int3 & land, int cost)
	{
		if(handler->gameState().day == day)
		{
			ASSERT_TRUE(handler->turnOrder->onPlayerEndsTurn(opponent));
		}
		ASSERT_EQ(handler->gameState().day, day + 1);
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(player));
		EXPECT_EQ(hero()->visitablePos(), land);
		EXPECT_FALSE(hero()->getTurnInfo(0)->hasWaterWalking());
		EXPECT_FALSE(hero()->hasNewHorizonsAdventureSpellCastToday());
		EXPECT_EQ(hero()->movementPointsRemaining(), hero()->movementPointsLimit());
		const auto renewedMana = hero()->getManaAvailable();
		const auto renewedMovement = hero()->movementPointsRemaining();
		ASSERT_GE(renewedMana, cost);
		cast();
		ASSERT_EQ(client->casts.size(), 1u);
		ASSERT_TRUE(server.acknowledgements.back());
		EXPECT_EQ(hero()->getManaAvailable(), renewedMana - cost);
		EXPECT_TRUE(hero()->getTurnInfo(0)->hasWaterWalking());
		EXPECT_TRUE(hero()->hasNewHorizonsAdventureSpellCastToday());
		cast();
		ASSERT_EQ(client->casts.size(), 2u);
		EXPECT_FALSE(server.acknowledgements.back());
		EXPECT_EQ(hero()->getManaAvailable(), renewedMana - cost);
		EXPECT_EQ(hero()->movementPointsRemaining(), renewedMovement);
		EXPECT_EQ(hero()->visitablePos(), land);
	}
};

TEST_F(NewHorizonsTravelRoundTripAITest, PaidWaterWalkRestoredMidCrossingLandsThenRenewsOnlyNextDay)
{
	const auto * enabled = std::getenv(enabledVariable);
	const bool requested = enabled || std::getenv("NH_TEST_WATER_WALK_ROUNDTRIP_SAVE_ROOT")
		|| std::getenv("NH_TEST_WATER_WALK_ROUNDTRIP_PRIVATE_ROOT")
		|| std::getenv("NH_TEST_WATER_WALK_ROUNDTRIP_SOURCE_ROOT");
	if(!requested)
		GTEST_SKIP() << "Explicit private travel-roundtrip invocation required";
	ASSERT_NE(enabled, nullptr);
	ASSERT_EQ(std::string_view(enabled), "1");
	ASSERT_EQ(::testing::GTEST_FLAG(filter),
		"NewHorizonsTravelRoundTripAITest.PaidWaterWalkRestoredMidCrossingLandsThenRenewsOnlyNextDay");
	const auto root = checkedSaveRoot();
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto land = hero()->visitablePos();
	const auto sea = land + int3(1, 0, 0);
	for(int x = -2; x <= 2; ++x)
		for(int y = -2; y <= 2; ++y)
			handler->gs->getMap().getTile(sea + int3(x, y, 0)).terrainType = ETerrainId::WATER;
	handler->gs->getMap().getTile(land).terrainType = ETerrainId::GRASS;
	hero()->setMovementPoints(30);
	const auto mana = hero()->getManaAvailable();
	const auto cost = hero()->getSpellCost(SpellID(SpellID::WATER_WALK).toSpell());
	ASSERT_EQ(cost, 30);
	ASSERT_GE(mana, cost * 2);
	cast();
	ASSERT_EQ(server.acknowledgements, std::vector<bool>{true});
	ASSERT_EQ(client->casts.size(), 1u);
	ASSERT_EQ(hero()->getManaAvailable(), mana - cost);
	moveTo(sea);
	ASSERT_TRUE(server.acknowledgements.back());
	ASSERT_EQ(hero()->visitablePos(), sea);
	ASSERT_EQ(hero()->movementPointsRemaining(), 15);
	ASSERT_EQ(client->moves.size(), 1u);
	ASSERT_NO_FATAL_FAILURE(roundTrip(root));
	ASSERT_EQ(hero()->visitablePos(), sea);
	ASSERT_EQ(hero()->movementPointsRemaining(), 15);
	ASSERT_EQ(hero()->getManaAvailable(), mana - cost);
	ASSERT_TRUE(hero()->hasNewHorizonsAdventureSpellCastToday());
	ASSERT_TRUE(hero()->getTurnInfo(0)->hasWaterWalking());
	const auto day = handler->gameState().day;
	EXPECT_FALSE(handler->turnOrder->onPlayerEndsTurn(player));
	EXPECT_EQ(handler->gameState().day, day);
	EXPECT_EQ(hero()->visitablePos(), sea);
	EXPECT_EQ(hero()->movementPointsRemaining(), 15);
	EXPECT_EQ(hero()->getManaAvailable(), mana - cost);
	const auto rng = bytes(*handler->randomizer);
	const auto fixtureRng = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero());
	ASSERT_TRUE(plan);
	ASSERT_EQ(plan->steps.size(), 1u);
	ASSERT_EQ(plan->steps.front().destination, land);
	client->beforeEndTurn = [&]()
	{
		EXPECT_EQ(hero()->visitablePos(), land);
		EXPECT_EQ(hero()->movementPointsRemaining(), 0);
		EXPECT_EQ(hero()->getManaAvailable(), mana - cost);
		EXPECT_EQ(bytes(*handler->randomizer), rng);
		EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	};
	{
		std::shared_lock lock(CGameState::mutex);
		gateway->endTurn();
	}
	ASSERT_EQ(client->moves.size(), 1u);
	EXPECT_EQ(client->moves.front().hid, heroID);
	EXPECT_EQ(client->moves.front().path, std::vector<int3>{hero()->convertFromVisitablePos(land)});
	EXPECT_EQ(client->moves.front().layer, EPathfindingLayer::LAND);
	EXPECT_FALSE(client->moves.front().transit);
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_TRUE(client->casts.empty());
	EXPECT_FALSE(gateway->status.haveTurn());
	ASSERT_TRUE(server.acknowledgements.back());
	ASSERT_NO_FATAL_FAILURE(expireAndRenew(day, land, cost));
}
}
