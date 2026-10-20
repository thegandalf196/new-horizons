/*
 * NewHorizonsTravelLandingAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "AI/Nullkiller2/AIGateway.h"
#include "lib/GameSettings.h"
#include "lib/bonuses/Bonus.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/pathfinder/NewHorizonsTravelSafety.h"
#include "lib/pathfinder/CPathfinder.h"
#include "lib/pathfinder/PathfinderOptions.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "server/processors/TurnOrderProcessor.h"

#include <algorithm>
#include <functional>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr PlayerColor OPPONENT(1);

class LandingServer final : public GameHandlerTestServer
{
public:
	explicit LandingServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER) {}
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

class LandingClient final : public IClient
{
	CGameHandler & handler;
	int sequence = 0;
public:
	explicit LandingClient(CGameHandler & handler) : handler(handler) {}
	std::vector<MoveHero> moves;
	int endTurns = 0;
	std::function<void(const MoveHero &)> beforeMove;
	std::function<void()> beforeEndTurn;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool waits) override
	{
		EXPECT_TRUE(waits);
		if(const auto * move = dynamic_cast<const MoveHero *>(&request))
		{
			moves.push_back(*move);
			if(beforeMove)
				beforeMove(*move);
		}
		else if(dynamic_cast<const EndTurn *>(&request))
		{
			++endTurns;
			if(beforeEndTurn)
				beforeEndTurn();
		}
		else
			throw std::runtime_error("Landing consumer submitted a non-movement/non-EndTurn request");
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++sequence;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return sequence;
	}
};

class NewHorizonsTravelLandingAITest : public TinyMapGameTest
{
protected:
	bool flyPolicy = true;
	bool waterPolicy = true;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * other = nullptr;
	std::unique_ptr<LandingServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<LandingClient> client;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	Services * gameServices() override { return LIBRARY; }
	void TearDown() override
	{
		if(server)
			server->gateway = nullptr;
		gateway.reset();
		callback.reset();
		client.reset();
		handler.reset();
		server.reset();
		TinyMapGameTest::TearDown();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		rules.Struct().erase("protectedAdventureBarriers");
		for(const auto & [name, active] : {std::pair{"core:fly", flyPolicy}, {"core:waterWalk", waterPolicy}})
		{
			auto & row = rules["adventureSpells"][name];
			row.Struct().erase("requireLegalDayEnd");
			if(active)
				row["requireLegalDayEnd"].Bool() = true;
		}
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OPPONENT)
			.hero({8, 8, 0}, HeroTypeID(0), PLAYER)
			.hero({20, 20, 0}, HeroTypeID(1), PLAYER)
			.hero({30, 30, 0}, HeroTypeID(2), OPPONENT);
		startWithMap(std::move(builder));
		hero = findHeroAt({8, 8, 0});
		other = findHeroAt({20, 20, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(other, nullptr);
		revealMap(PLAYER);
		server = std::make_unique<LandingServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->turnOrder->addPlayer(PLAYER);
		handler->turnOrder->addPlayer(OPPONENT);
		handler->onNewTurn();
		handler->turnOrder->onGameStarted();
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PLAYER));
		client = std::make_unique<LandingClient>(*handler);
		callback = makeCallback(PLAYER, client.get());
		gateway = std::make_unique<NK2AI::AIGateway>();
		gateway->initGameInterface(std::shared_ptr<Environment>(), callback);
		gateway->status.startedTurn();
		server->gateway = gateway.get();
	}
	void interruptOverWater(CGHeroInstance * actor, EPathfindingLayer layer)
	{
		const auto source = layer == EPathfindingLayer::AIR ? SpellID::FLY : SpellID::WATER_WALK;
		const auto type = layer == EPathfindingLayer::AIR ? BonusType::FLYING_MOVEMENT : BonusType::WATER_WALKING;
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_DAY, type,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(source))));
		const auto land = actor->visitablePos();
		map()->getTile(land).terrainType = ETerrainId::GRASS;
		const auto sea = land + int3(1, 0, 0);
		map()->getTile(sea).terrainType = ETerrainId::WATER;
		actor->setMovementPoints(30);
		ASSERT_TRUE(handler->moveHero(actor->id, actor->convertFromVisitablePos(sea),
			EMovementMode::STANDARD, true, PLAYER, layer));
		ASSERT_EQ(actor->visitablePos(), sea);
		ASSERT_EQ(actor->movementPointsRemaining(), 15);
	}
	void endTurn()
	{
		// Same ownership as makeTurn: callback realization and waitTillFree
		// temporarily release this shared game-state guard.
		std::shared_lock lock(CGameState::mutex);
		gateway->endTurn();
	}
	void acquireForcedMarch()
	{
		const std::string parent = "new-horizons:logistics";
		const std::string perk = parent + ".forcedMarch";
		const auto skill = SecondarySkill(SecondarySkill::decode(parent));
		ASSERT_GE(skill.getNum(), 0);
		ASSERT_EQ(hero->getPerkSkillRank(parent), 0);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->getPerkState().canAcquirePerk(parent, perk, hero->getPerkSkillRank(parent)));
		hero->applyPerkSelection({parent, perk});
		ASSERT_TRUE(hero->hasActivePerk(parent, perk));
	}
	template<typename T> static auto bytes(T & value)
	{
		CMemorySerializer serialized;
		serialized.oser & value;
		return serialized.extractBuffer();
	}
	void expectSubmitted(const newHorizonsTravel::Plan & plan, const CGHeroInstance & actor, size_t offset)
	{
		ASSERT_GE(client->moves.size(), offset + plan.steps.size());
		for(size_t index = 0; index < plan.steps.size(); ++index)
		{
			const auto & step = plan.steps[index];
			const auto & request = client->moves[offset + index];
			EXPECT_EQ(request.hid, actor.id);
			EXPECT_EQ(request.path, std::vector<int3>{actor.convertFromVisitablePos(step.destination)});
			EXPECT_EQ(request.layer, step.layer);
			EXPECT_EQ(request.transit, step.layer != EPathfindingLayer::LAND);
		}
	}
};
}

TEST_F(NewHorizonsTravelLandingAITest, InterruptedRoutesLandEveryOwnedTravelerBeforeAuthoritativeEndTurn)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(other, EPathfindingLayer::WATER));
	const auto first = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	const auto second = newHorizonsTravel::planRequiredLanding(*callback, *other);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_FALSE(first->steps.empty());
	ASSERT_FALSE(second->steps.empty());
	const auto serverRng = bytes(*handler->randomizer);
	const auto gameRng = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto mana = hero->getManaAvailable();
	const auto day = gameState()->getCalendar().getCurrentDay();
	client->beforeEndTurn = [&]()
	{
		EXPECT_EQ(hero->movementPointsRemaining(), first->steps.back().movementRemaining);
		EXPECT_EQ(other->movementPointsRemaining(), second->steps.back().movementRemaining);
		EXPECT_TRUE(map()->getTile(hero->visitablePos()).isLand());
		EXPECT_TRUE(map()->getTile(other->visitablePos()).isLand());
		EXPECT_EQ(bytes(*handler->randomizer), serverRng);
		EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ(hero->getManaAvailable(), mana);
		EXPECT_EQ(gameState()->getCalendar().getCurrentDay(), day);
	};
	endTurn();
	ASSERT_NO_FATAL_FAILURE(expectSubmitted(*first, *hero, 0));
	ASSERT_NO_FATAL_FAILURE(expectSubmitted(*second, *other, first->steps.size()));
	EXPECT_EQ(client->moves.size(), first->steps.size() + second->steps.size());
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
	EXPECT_FALSE(handler->turnOrder->isPlayerMakingTurn(PLAYER));
	ASSERT_EQ(server->acknowledgements.size(), client->moves.size() + 1);
	EXPECT_TRUE(std::all_of(server->acknowledgements.begin(), server->acknowledgements.end(), [](bool accepted) { return accepted; }));
}

TEST_F(NewHorizonsTravelLandingAITest, StrandedZeroMovementRetainsTurnButOtherTravelerStillLands)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(other, EPathfindingLayer::WATER));
	// Characterize an already-stranded old state, not permission for a new route
	// to spend its reserve. Consumer must neither restore MPs nor teleport it.
	hero->setMovementPoints(0);
	const auto stranded = hero->visitablePos();
	ASSERT_FALSE(newHorizonsTravel::planRequiredLanding(*callback, *hero));
	endTurn();
	EXPECT_EQ(hero->visitablePos(), stranded);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_TRUE(map()->getTile(other->visitablePos()).isLand());
	EXPECT_FALSE(client->moves.empty());
	EXPECT_EQ(client->endTurns, 0);
	EXPECT_TRUE(gateway->status.haveTurn());
	EXPECT_TRUE(handler->turnOrder->isPlayerMakingTurn(PLAYER));
}

TEST_F(NewHorizonsTravelLandingAITest, AuthorityRejectsChangedStepWithoutEndTurnOrRetry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	const auto position = hero->visitablePos();
	client->beforeMove = [&](const MoveHero & request)
	{
		ASSERT_EQ(request.path.size(), 1);
		// The actual server sees a newly impassable destination after planning.
		map()->getTile(hero->convertToVisitablePos(request.path.front())).terrainType = ETerrainId::ROCK;
	};
	endTurn();
	EXPECT_EQ(client->moves.size(), 1);
	EXPECT_EQ(client->endTurns, 0);
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->movementPointsRemaining(), 15);
	ASSERT_FALSE(server->acknowledgements.empty());
	EXPECT_FALSE(server->acknowledgements.front());
	EXPECT_TRUE(gateway->status.haveTurn());
}

TEST_F(NewHorizonsTravelLandingAITest, EligibleAirOnGroundNeedsNoFictitiousLandCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	map()->getTile(hero->visitablePos()).terrainType = ETerrainId::GRASS;
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	ASSERT_TRUE(plan);
	ASSERT_TRUE(plan->steps.empty());
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::AIR);
	endTurn();
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
}

TEST_F(NewHorizonsTravelLandingAITest, HistoricalMixedPolicyConsumesRequestedLayersWithoutInventingFlightReceipt)
{
	flyPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	ASSERT_TRUE(plan);
	ASSERT_FALSE(plan->steps.empty());
	endTurn();
	ASSERT_NO_FATAL_FAILURE(expectSubmitted(*plan, *hero, 0));
	EXPECT_EQ(client->moves.size(), plan->steps.size());
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
}

TEST_F(NewHorizonsTravelLandingAITest, EndTurnRejectedAfterFinalReadbackIsSubmittedOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	client->beforeEndTurn = [&]()
	{
		// A live authoritative eligibility change after the final consumer check.
		map()->getTile(hero->visitablePos()).terrainType = ETerrainId::WATER;
	};
	endTurn();
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(client->endTurns, 1);
	ASSERT_FALSE(server->acknowledgements.empty());
	EXPECT_FALSE(server->acknowledgements.back());
	EXPECT_TRUE(gateway->status.haveTurn());
	EXPECT_TRUE(handler->turnOrder->isPlayerMakingTurn(PLAYER));
}

TEST_F(NewHorizonsTravelLandingAITest, AbsentCapturedPoliciesDoNotForceHistoricalTravelerToLand)
{
	flyPolicy = false;
	waterPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	const auto position = hero->visitablePos();
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	ASSERT_TRUE(plan);
	ASSERT_TRUE(plan->steps.empty());
	endTurn();
	EXPECT_TRUE(client->moves.empty());
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
}

TEST_F(NewHorizonsTravelLandingAITest, TerminalForcedMarchGrantMatchesExactMovementAndEarnedReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireForcedMarch());
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	ASSERT_TRUE(plan);
	ASSERT_FALSE(plan->steps.empty());
	ASSERT_TRUE(plan->steps.back().forcedMarch);
	const auto grant = std::max(0, hero->movementPointsLimit()) / 10;
	ASSERT_GT(grant, 0);
	EXPECT_EQ(plan->steps.back().movementRemaining, grant);
	client->beforeEndTurn = [&]()
	{
		EXPECT_EQ(hero->movementPointsRemaining(), grant);
		EXPECT_EQ(hero->getNewHorizonsForcedMarchLastUseDay(), plan->day);
		EXPECT_EQ(hero->getNewHorizonsForcedMarchPenaltyDay(), plan->day);
	};
	endTurn();
	ASSERT_NO_FATAL_FAILURE(expectSubmitted(*plan, *hero, 0));
	EXPECT_EQ(client->moves.size(), plan->steps.size());
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
	EXPECT_TRUE(std::all_of(server->acknowledgements.begin(), server->acknowledgements.end(), [](bool accepted) { return accepted; }));
}

TEST_F(NewHorizonsTravelLandingAITest, AlreadyEarnedForcedMarchDoesNotAnnotateOrGrantAgainOnLanding)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireForcedMarch());
	const auto origin = hero->visitablePos();
	const auto land = origin + int3(1, 0, 0);
	map()->getTile(origin).terrainType = ETerrainId::GRASS;
	map()->getTile(land).terrainType = ETerrainId::GRASS;
	const PathfinderOptions options(*callback);
	CPathfinderHelper costs(*callback, hero, options);
	const auto cost = costs.getMovementCost(origin, land, EPathfindingLayer::LAND, hero->movementPointsLimit());
	ASSERT_GT(cost, 0);
	hero->setMovementPoints(cost);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(land),
		EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::LAND));
	const auto day = gameState()->getCalendar().getCurrentDay();
	ASSERT_EQ(hero->getNewHorizonsForcedMarchLastUseDay(), day);
	ASSERT_EQ(hero->getNewHorizonsForcedMarchPenaltyDay(), day);
	ASSERT_GT(hero->movementPointsRemaining(), 0);
	// Start a distinct controlled crossing on land. This is fixture setup, not
	// consumer rescue, and earns neither a second opportunity nor a new receipt.
	ASSERT_NO_FATAL_FAILURE(interruptOverWater(hero, EPathfindingLayer::AIR));
	const auto plan = newHorizonsTravel::planRequiredLanding(*callback, *hero);
	ASSERT_TRUE(plan);
	ASSERT_FALSE(plan->steps.empty());
	EXPECT_FALSE(plan->steps.back().forcedMarch);
	EXPECT_EQ(plan->steps.back().movementRemaining, 0);
	client->beforeEndTurn = [&]()
	{
		EXPECT_EQ(hero->movementPointsRemaining(), 0);
		EXPECT_EQ(hero->getNewHorizonsForcedMarchLastUseDay(), day);
		EXPECT_EQ(hero->getNewHorizonsForcedMarchPenaltyDay(), day);
	};
	endTurn();
	ASSERT_NO_FATAL_FAILURE(expectSubmitted(*plan, *hero, 0));
	EXPECT_EQ(client->moves.size(), plan->steps.size());
	EXPECT_EQ(client->endTurns, 1);
	EXPECT_FALSE(gateway->status.haveTurn());
}
