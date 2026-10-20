/*
 * AdventureSpellExecutionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "SpellPointTestUtils.h"
#include "mock/GameHandlerTestServer.h"
#include "AI/Nullkiller2/Goals/AdventureSpellCast.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "AI/Nullkiller2/Pathfinding/Actions/AdventureSpellCastMovementActions.h"
#include "lib/GameSettings.h"
#include "lib/CRandomGenerator.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/callback/IClient.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/pathfinder/TurnInfo.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/serializer/CTypeList.h"
#include "lib/spells/CSpell.h"
#include "server/CGameHandler.h"

#include <functional>
#include <shared_mutex>
#include <stdexcept>
#include <vector>

namespace
{
constexpr PlayerColor PLAYER(0);

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class SpellServer final : public GameHandlerTestServer
{
public:
	explicit SpellServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER) {}
	NK2AI::AIGateway * gateway = nullptr;
	bool wrongReplyID = false;
	std::vector<bool> accepted;
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * reply = dynamic_cast<PackageApplied *>(&pack))
		{
			accepted.push_back(reply->result);
			auto observed = *reply;
			if(wrongReplyID)
				observed.requestID += 100;
			gateway->requestRealized(&observed);
		}
	}
};

class SpellClient final : public IClient
{
	CGameHandler & handler;
	int sequence = 0;
public:
	explicit SpellClient(CGameHandler & handler) : handler(handler) {}
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<CastAdvSpell> casts;
	std::vector<MoveHero> moves;
	std::function<void()> beforeCast;
	bool earlyReply = false;
	bool throwOnCast = false;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool waits) override
	{
		EXPECT_TRUE(waits);
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++sequence;
		if(auto * cast = dynamic_cast<CastAdvSpell *>(incoming.get()))
		{
			casts.push_back(*cast);
			if(beforeCast)
				beforeCast();
			if(throwOnCast)
			{
				gateway->requestSent(incoming.get(), sequence);
				throw std::runtime_error("Injected submission failure");
			}
		}
		else if(auto * move = dynamic_cast<MoveHero *>(incoming.get()))
			moves.push_back(*move);
		else
			throw std::runtime_error("Cast fixture refuses unrelated requests");
		if(!earlyReply)
			gateway->requestSent(incoming.get(), sequence);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		if(earlyReply)
			gateway->requestSent(incoming.get(), sequence);
		return sequence;
	}
};

class AdventureSpellExecutionTest : public NullkillerTest
{
protected:
	bool historical = false;
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<SpellServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<SpellClient> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void TearDown() override
	{
		if(server)
			server->gateway = nullptr;
		gateway.reset(); client.reset(); handler.reset(); server.reset();
		NullkillerTest::TearDown();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(historical)
		{
			// Capture a coherent pre-feature world before hero initialization.
			JsonNode plainRules;
			plainRules.Struct();
			plainRules.setOverrideFlag(true);
			for(const auto setting : {EGameSettings::MAGIC_NEW_HORIZONS, EGameSettings::HEROES_NEW_HORIZONS,
				EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
				EGameSettings::HEROES_NEW_HORIZONS_PERKS})
				loaded->overrideGameSetting(setting, plainRules);
			return;
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(0), 1}})
			.heroPrimary(10, 10, 10, 20)
			.heroSpells({SpellID::WATER_WALK, SpellID::FLY})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PLAYER); ASSERT_NE(hero, nullptr);
		setTestSpellPointTotal(hero, 200); hero->setMovementPoints(200);
		revealMap(PLAYER);
		gameState()->actingPlayers.insert(PLAYER);
		server = std::make_unique<SpellServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		client = std::make_unique<SpellClient>(*handler);
		gateway = makeGateway(PLAYER, client.get());
		gateway->cc->waitTillRealize = false; // Explicit restoration contract, not initGameInterface's default.
		server->gateway = client->gateway = gateway.get();
	}
	void cast(SpellID spell)
	{
		std::shared_lock lock(CGameState::mutex);
		NK2AI::Goals::AdventureSpellCast(hero, spell).accept(gateway.get());
	}
	auto rngState()
	{
		return std::pair(serialized(*handler->randomizer),
			serialized(dynamic_cast<CRandomGenerator &>(*getRNG())));
	}
	void checkAccepted(SpellID spell, bool initialWait = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		gateway->cc->waitTillRealize = initialWait;
		const auto mana = hero->getManaAvailable();
		const auto cost = hero->getSpellCost(spell.toSpell());
		EXPECT_THROW(cast(spell), NK2AI::goalFulfilledException);
		ASSERT_EQ(client->casts.size(), 1u);
		EXPECT_EQ(client->casts.front().pos, int3(-1, -1, -1));
		EXPECT_EQ(server->accepted, std::vector<bool>({true}));
		EXPECT_EQ(hero->getManaAvailable(), mana - cost);
		EXPECT_TRUE(hero->hasNewHorizonsAdventureSpellCastToday());
		EXPECT_EQ(gateway->cc->waitTillRealize, initialWait);
	}
	void checkFuture(bool fly)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		gateway->cc->waitTillRealize = true; // Normal AI movement ownership, unlike cast-restoration controls.
		const auto source = hero->visitablePos();
		const auto approach = source + int3(1, 0, 0);
		map()->getTile(source).terrainType = ETerrainId::GRASS;
		map()->getTile(approach).terrainType = ETerrainId::GRASS;
		hero->setMovementPoints(22);
		const auto mana = hero->getManaAvailable();
		const auto rng = rngState();
		NK2AI::AIPath path;
		path.targetHero = hero; path.heroArmy = hero; path.chainMask = 1; path.exchangeCount = 1;
		path.targetObjectDanger = path.armyLoss = path.targetObjectArmyLoss = 0;
		NK2AI::AIPathNodeInfo future{};
		future.targetHero = hero; future.turns = 1; future.coord = approach + int3(1, 0, 0);
		future.parentIndex = -1; future.chainMask = 1; future.cost = 1.1f;
		future.layer = fly ? EPathfindingLayer::AIR : EPathfindingLayer::WATER;
		future.specialAction = fly
			? std::shared_ptr<const NK2AI::SpecialAction>(std::make_shared<NK2AI::AIPathfinding::AirWalkingAction>(hero, SpellID(SpellID::FLY)))
			: std::shared_ptr<const NK2AI::SpecialAction>(std::make_shared<NK2AI::AIPathfinding::WaterWalkingAction>(hero, SpellID(SpellID::WATER_WALK)));
		auto current = future;
		current.specialAction.reset(); current.turns = 0; current.coord = approach;
		current.layer = EPathfindingLayer::LAND; current.cost = 0.05f; current.parentIndex = 0;
		path.nodes = {future, current};
		{
			std::shared_lock lock(CGameState::mutex);
			NK2AI::Goals::ExecuteHeroChain(path).accept(gateway.get());
		}
		ASSERT_EQ(client->moves.size(), 1u);
		EXPECT_EQ(hero->visitablePos(), approach);
		EXPECT_EQ(hero->movementPointsRemaining(), 12);
		EXPECT_EQ(hero->getManaAvailable(), mana);
		EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
		EXPECT_TRUE(client->casts.empty());
		EXPECT_TRUE(gateway->nullkiller->isHeroLocked(hero));
		EXPECT_TRUE(gateway->cc->waitTillRealize);
		EXPECT_EQ(rngState(), rng);
	}
};
}

TEST_F(AdventureSpellExecutionTest, TargetlessWaterWalkFulfillsOnlyAfterAcceptedAuthoritativeCast)
{
	checkAccepted(SpellID(SpellID::WATER_WALK));
}

TEST_F(AdventureSpellExecutionTest, TargetlessFlyFulfillsOnlyAfterAcceptedAuthoritativeCast)
{
	checkAccepted(SpellID(SpellID::FLY), true);
}

TEST_F(AdventureSpellExecutionTest, SpentDailyOpportunityRejectsTargetlessActionsBeforeSubmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hero->setNewHorizonsAdventureSpellCastToday(true);
	const auto mana = hero->getManaAvailable();
	const auto rng = rngState();
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), NK2AI::cannotFulfillGoalException);
	EXPECT_THROW(cast(SpellID(SpellID::FLY)), NK2AI::cannotFulfillGoalException);
	EXPECT_TRUE(client->casts.empty()); EXPECT_TRUE(server->accepted.empty());
	EXPECT_EQ(hero->getManaAvailable(), mana);
	EXPECT_FALSE(gateway->cc->waitTillRealize);
	EXPECT_EQ(rngState(), rng);
}

TEST_F(AdventureSpellExecutionTest, AuthoritativeRejectionAfterPreflightDoesNotFulfillOrSpend)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = hero->getManaAvailable();
	client->beforeCast = [this] { hero->setNewHorizonsAdventureSpellCastToday(true); };
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), NK2AI::cannotFulfillGoalException);
	ASSERT_EQ(client->casts.size(), 1u);
	EXPECT_EQ(server->accepted, std::vector<bool>({false}));
	EXPECT_EQ(hero->getManaAvailable(), mana);
	EXPECT_FALSE(hero->getTurnInfo(0)->hasWaterWalking());
	EXPECT_FALSE(gateway->cc->waitTillRealize);
}

TEST_F(AdventureSpellExecutionTest, SubmissionExceptionClearsReceiptAndRestoresWaitForNextLegalCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = hero->getManaAvailable();
	const auto cost = hero->getSpellCost(SpellID(SpellID::WATER_WALK).toSpell());
	client->throwOnCast = true;
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), std::runtime_error);
	EXPECT_FALSE(gateway->cc->waitTillRealize);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
	client->throwOnCast = false;
	client->beforeCast = [this]()
	{
		// A late positive answer for failed request1 cannot fulfill request2.
		PackageApplied stale(PLAYER, 1, CTypeList::getInstance().getTypeID<CastAdvSpell>(nullptr), true);
		gateway->requestRealized(&stale);
	};
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), NK2AI::goalFulfilledException);
	EXPECT_EQ(client->casts.size(), 2u);
	EXPECT_EQ(server->accepted, std::vector<bool>({true}));
	EXPECT_EQ(hero->getManaAvailable(), mana - cost);
	EXPECT_FALSE(gateway->cc->waitTillRealize);
}

TEST_F(AdventureSpellExecutionTest, ImmediateReplyBeforeRequestSentStillRequiresTheExactSentID)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	client->earlyReply = true;
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), NK2AI::goalFulfilledException);
	EXPECT_EQ(client->casts.size(), 1u);
	EXPECT_EQ(server->accepted, std::vector<bool>({true}));
}

TEST_F(AdventureSpellExecutionTest, UnmatchedAcknowledgementCannotFulfillEvenWhenEffectApplied)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	server->wrongReplyID = true;
	EXPECT_THROW(cast(SpellID(SpellID::WATER_WALK)), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(server->accepted, std::vector<bool>({true}));
	EXPECT_TRUE(hero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(client->casts.size(), 1u);
	EXPECT_FALSE(gateway->cc->waitTillRealize);
}

TEST_F(AdventureSpellExecutionTest, FutureWaterWalkKeepsCurrentApproachAndDefersWithLeftoverMovement)
{
	checkFuture(false);
}

TEST_F(AdventureSpellExecutionTest, FutureFlyKeepsCurrentApproachAndDefersWithLeftoverMovement)
{
	checkFuture(true);
}

TEST_F(AdventureSpellExecutionTest, HistoricalRulesRetainTheirExistingRepeatCastAdmission)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::WATER_WALK);
	const auto mana = hero->getManaAvailable();
	const auto cost = hero->getSpellCost(spell.toSpell());
	EXPECT_THROW(cast(spell), NK2AI::goalFulfilledException);
	EXPECT_THROW(cast(spell), NK2AI::goalFulfilledException);
	EXPECT_EQ(client->casts.size(), 2u);
	EXPECT_EQ(server->accepted, std::vector<bool>({true, true}));
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(hero->getManaAvailable(), mana - 2 * cost);
}
