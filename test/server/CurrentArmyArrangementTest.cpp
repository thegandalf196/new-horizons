/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../server/CGameHandler.h"
#include <limits>

namespace
{
// The V4 admission suite remains visible historical debt. These regressions use
// the current captured profile: coordination thresholds never restrict armies.
class LeadershipRecordingServer final : public GameHandlerTestServer
{
public:
	explicit LeadershipRecordingServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PlayerColor(0)) {}
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<RebalanceStacks *>(&pack)) ++rebalancePacks;
		if(dynamic_cast<SystemMessage *>(&pack)) ++systemMessages;
		GameHandlerTestServer::applyPack(pack);
	}
	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		if(const auto * applied = dynamic_cast<PackageApplied *>(&pack)) responses.push_back(*applied);
		GameHandlerTestServer::sendPack(pack, connection);
	}
	std::vector<PackageApplied> responses;
	int rebalancePacks = 0;
	int systemMessages = 0;
};

class CurrentArmyArrangementTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}
	static HeroTypeID heroType(const char * id) { return HeroTypeID(HeroTypeID::decode(id)); }
	static FactionID faction(const char * id) { return FactionID(FactionID::decode(id)); }
};
}

TEST_F(CurrentArmyArrangementTest, NumericSplitRejectsNegativeDeltaAndUnavailableUnitsButAllowsOversizedStacks)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 17}, {pikeman, 2}})
		.playerActive(PlayerColor(1)).town({25, 25, 0}, faction("core:castle"), PlayerColor(1));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto capacity = newHorizonsHeroes::leadershipStackReadout(hero, pikeman, 0);
	ASSERT_TRUE(capacity.active);
	ASSERT_EQ(hero->getCapabilityRules()["rulesetVersion"].Integer(), 7);
	hero->setStackCount(SlotID(0), static_cast<TQuantity>(capacity.capacity));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));

	// An explicit numeric request that lowers the destination's current count
	// is malformed/stale, not a reverse transfer.
	ArrangeStacks negativeDelta(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity.capacity - 1);
	negativeDelta.player = PlayerColor(0);
	negativeDelta.requestID = 44;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, negativeDelta);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity.capacity);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);

	// A zero-delta request is a successful no-op and emits no state pack.
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks zeroDelta(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity.capacity);
	zeroDelta.player = PlayerColor(0);
	zeroDelta.requestID = 45;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, zeroDelta);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity.capacity);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
	EXPECT_EQ(server.systemMessages, 0);

	// Numeric requests remain exact. An unavailable third creature is rejected
	// without mutation; an available pair may exceed the coordination threshold.
	hero->setStackCount(SlotID(0), capacity.capacity - 1);
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks unavailable(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity.capacity + 2);
	unavailable.player = PlayerColor(0);
	unavailable.requestID = 46;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, unavailable);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity.capacity - 1);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks overCapacity(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity.capacity + 1);
	overCapacity.player = PlayerColor(0);
	overCapacity.requestID = 47;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, overCapacity);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity.capacity + 1);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 1);
	EXPECT_EQ(server.systemMessages, 0);
	// Leadership is not a count cap, but the engine's integer limit remains.
	ASSERT_TRUE(gameState()->actingPlayers.contains(PlayerColor(0)));
	hero->setStackCount(SlotID(0), std::numeric_limits<TQuantity>::max());
	ASSERT_TRUE(hero->setCreature(SlotID(1), pikeman, 1));
	server.responses.clear();
	ArrangeStacks overflow(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	overflow.player = PlayerColor(0);
	overflow.requestID = 48;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, overflow);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), std::numeric_limits<TQuantity>::max());
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	EXPECT_EQ(server.rebalancePacks, 1);

}

TEST_F(CurrentArmyArrangementTest, StaleNumericSplitDoesNotMutateArmy)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 10}, {pikeman, 2}})
		.playerActive(PlayerColor(1)).town({25, 25, 0}, faction("core:castle"), PlayerColor(1));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const TQuantity destinationBefore = hero->getStackCount(SlotID(0));
	hero->eraseStack(SlotID(1)); // the client request was built before this state change

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	ArrangeStacks stale(3, SlotID(1), SlotID(0), hero->id, hero->id, destinationBefore + 1);
	stale.player = PlayerColor(0);
	stale.requestID = 47;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, stale);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), destinationBefore);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
}

TEST_F(CurrentArmyArrangementTest, CrossHeroMergeKeepsRequiredLastCreatureWhileExceedingCoordinationThreshold)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	const int3 townPosition(12, 12, 0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town(townPosition, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.hero({6, 6, 0}, heroType("core:valeska"), player)
		.playerActive(PlayerColor(1)).town({25, 25, 0}, faction("core:castle"), PlayerColor(1));
	startWithMap(std::move(builder));

	auto * town = findFirst<CGTownInstance>();
	auto heroes = findAll<CGHeroInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(heroes.size(), 2u);
	auto * destination = heroes[0];
	auto * source = heroes[1];
	destination->clearSlots();
	source->clearSlots();
	const auto capacity = newHorizonsHeroes::leadershipStackReadout(destination, pikeman, 0);
	ASSERT_TRUE(capacity.active);
	ASSERT_EQ(destination->getCapabilityRules()["rulesetVersion"].Integer(), 7);
	ASSERT_GT(capacity.capacity, 1);
	ASSERT_TRUE(destination->setCreature(SlotID(0), pikeman, static_cast<TQuantity>(capacity.capacity + 5)));
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 2));
	SetHeroesInTown setHeroes;
	setHeroes.tid = town->id;
	setHeroes.visiting = destination->id;
	setHeroes.garrison = source->id;
	gameState()->apply(setHeroes);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	ArrangeStacks merge(2, SlotID(0), SlotID(0), source->id, destination->id, 0);
	merge.player = player;
	merge.requestID = 48;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, merge);

	EXPECT_EQ(destination->getStackCount(SlotID(0)), capacity.capacity + 6);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 1);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}
