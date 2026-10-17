/*
 * ArmyExchangeExecutionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "AI/Nullkiller2/Helpers/ArmyExchangeExecution.h"
#include "AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "lib/CPlayerState.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"

#include <functional>
#include <map>
#include <stdexcept>

namespace
{
constexpr PlayerColor PLAYER(0);
using namespace NK2AI::armyFormation;
using Army = std::map<SlotID, std::pair<CreatureID, TQuantity>>;

/// Real callback requests enter the authoritative handler; faults model a failed/stale exchange.
class ExchangeLoopback final : public IClient
{
	CGameHandler & handler;

public:
	std::vector<ArrangeStacks> requests;
	std::vector<bool> waits;
	std::function<void()> afterApplied;
	bool throwOnRequest = false;

	explicit ExchangeLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool wait) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = static_cast<int>(requests.size()) + 1;
		const auto * transfer = dynamic_cast<const ArrangeStacks *>(incoming.get());
		EXPECT_NE(transfer, nullptr);
		if(!transfer)
			throw std::runtime_error("Unexpected request from physical exchange executor");
		requests.push_back(*transfer);
		waits.push_back(wait);
		if(throwOnRequest)
			throw std::runtime_error("Injected callback failure");
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		if(afterApplied)
			afterApplied();
		return incoming->requestID;
	}
};

/// Focused executor tests reuse the existing live world/server fixture without starting a battle.
class ArmyExchangeExecutionTest : public HeroCommandFixture
{
protected:
	CGHeroInstance * receiver = nullptr;
	CGTownInstance * donor = nullptr;
	std::unique_ptr<ExchangeLoopback> client;
	std::shared_ptr<CCallback> callback;
	CreatureID pikeman;
	CreatureID archer;
	CreatureID guard;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		pikeman = creatureByName("core:pikeman");
		archer = creatureByName("core:archer");
		guard = creatureByName("core:halberdier");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, PLAYER).townGarrison({{pikeman, 10}, {archer, 4}})
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
			.heroGarrison({{guard, 1}})
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({{guard, 1}});
		startWithMap(std::move(builder));
		receiver = findHeroByOwner(PLAYER);
		donor = findFirst<CGTownInstance>();
		ASSERT_NE(receiver, nullptr);
		ASSERT_NE(donor, nullptr);
		receiver->clearSlots();
		ASSERT_TRUE(receiver->setCreature(SlotID(1), guard, 1));
		donor->clearSlots();
		ASSERT_TRUE(donor->setCreature(SlotID(0), pikeman, 10));
		ASSERT_TRUE(donor->setCreature(SlotID(2), archer, 4));
		donor->setVisitingHero(receiver);
		const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
		ASSERT_TRUE(capacity);
		ASSERT_GE(capacity->maximum, 10);
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		client = std::make_unique<ExchangeLoopback>(*gameHandler);
		callback = makeCallback(PLAYER, client.get());
	}

	static Army snapshot(const CArmedInstance * army)
	{
		Army result;
		for(const auto & [slot, stack] : army->Slots())
			result[slot] = {stack->getCreatureID(), stack->getCount()};
		return result;
	}

	std::map<CreatureID, int64_t> totals() const
	{
		std::map<CreatureID, int64_t> result;
		for(const auto * army : {static_cast<const CArmedInstance *>(receiver),
			static_cast<const CArmedInstance *>(donor)})
			for(const auto & [slot, stack] : army->Slots())
				result[stack->getCreatureID()] += stack->getCount();
		return result;
	}

	ArmyExchangeProjection partialThenWhole() const
	{
		ArmyExchangeProjection plan;
		plan.transfers = {
			{ArmyExchangeSide::SOURCE, SlotID(0), SlotID(0), pikeman, 10, CreatureID::NONE, 0, 5, 5, false},
			{ArmyExchangeSide::SOURCE, SlotID(2), SlotID(2), archer, 4, CreatureID::NONE, 0, 4, 4, false}
		};
		return plan;
	}

	void execute(const ArmyExchangeProjection & plan)
	{
		executeArmyExchange(callback, receiver, donor, plan);
		EXPECT_FALSE(callback->waitTillRealize);
		for(bool wait : client->waits)
			EXPECT_TRUE(wait);
	}

	void TearDown() override
	{
		callback.reset();
		client.reset();
		HeroCommandFixture::TearDown();
	}
};
}

TEST_F(ArmyExchangeExecutionTest, OrderedPartialAndWholeRequestsConserveEachCreature)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = totals();
	const auto assertActiveGame = [this]()
	{
		EXPECT_EQ(gameState()->getPlayerState(PLAYER)->status, EPlayerStatus::INGAME);
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(1))->status, EPlayerStatus::INGAME);
		EXPECT_TRUE(gameState()->actingPlayers.contains(PLAYER));
	};
	assertActiveGame();
	client->afterApplied = assertActiveGame;
	execute(partialThenWhole());
	ASSERT_EQ(client->requests.size(), 2);
	EXPECT_EQ(client->requests[0].what, 3);
	EXPECT_EQ(client->requests[0].val, 5);
	EXPECT_EQ(client->requests[0].p1, SlotID(0));
	EXPECT_EQ(client->requests[0].p2, SlotID(0));
	EXPECT_EQ(client->requests[0].id1, donor->id);
	EXPECT_EQ(client->requests[0].id2, receiver->id);
	EXPECT_EQ(client->requests[0].player, PLAYER);
	EXPECT_EQ(client->requests[1].what, 1);
	EXPECT_EQ(client->requests[1].p1, SlotID(2));
	EXPECT_EQ(client->requests[1].p2, SlotID(2));
	EXPECT_EQ(client->requests[1].id1, donor->id);
	EXPECT_EQ(client->requests[1].id2, receiver->id);
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(receiver->getStackCount(SlotID(2)), 4);
	EXPECT_EQ(donor->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(donor->getStackPtr(SlotID(2)), nullptr);
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, WholeSameCreatureMergeUsesExistingDestinationCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, 2));
	const auto before = totals();
	ArmyExchangeProjection plan;
	plan.transfers = {{ArmyExchangeSide::SOURCE, SlotID(0), SlotID(0), pikeman, 10, pikeman, 2, 10, 12, false}};
	const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GE(capacity->maximum, 12);
	execute(plan);
	ASSERT_EQ(client->requests.size(), 1);
	EXPECT_EQ(client->requests[0].what, 2);
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 12);
	EXPECT_EQ(donor->getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, DifferingCreatureSwapPreservesBothPhysicalStacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(receiver->setCreature(SlotID(0), guard, 1));
	const auto before = totals();
	ArmyExchangeProjection plan;
	plan.transfers = {{ArmyExchangeSide::SOURCE, SlotID(0), SlotID(0), pikeman, 10, guard, 1, 10, 10, true}};
	execute(plan);
	ASSERT_EQ(client->requests.size(), 1);
	EXPECT_EQ(client->requests[0].what, 1);
	ASSERT_NE(receiver->getStackPtr(SlotID(0)), nullptr);
	ASSERT_NE(donor->getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(receiver->getStackPtr(SlotID(0))->getCreatureID(), pikeman);
	EXPECT_EQ(donor->getStackPtr(SlotID(0))->getCreatureID(), guard);
	EXPECT_EQ(donor->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, ReceiverSideTransferUsesOppositeDirectionAndFinalSplitCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, 5));
	const auto before = totals();
	ArmyExchangeProjection plan;
	plan.transfers = {{ArmyExchangeSide::RECEIVER, SlotID(0), SlotID(0), pikeman, 5, pikeman, 10, 2, 12, false}};
	execute(plan);
	ASSERT_EQ(client->requests.size(), 1);
	EXPECT_EQ(client->requests[0].id1, receiver->id);
	EXPECT_EQ(client->requests[0].id2, donor->id);
	EXPECT_EQ(client->requests[0].val, 12);
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(donor->getStackCount(SlotID(0)), 12);
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, StaleSourceCountStopsBeforeAnyRequest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto plan = partialThenWhole();
	plan.transfers[0].expectedSourceCount = 9;
	const auto heroBefore = snapshot(receiver), townBefore = snapshot(donor);
	execute(plan);
	EXPECT_TRUE(client->requests.empty());
	EXPECT_EQ(snapshot(receiver), heroBefore);
	EXPECT_EQ(snapshot(donor), townBefore);
}

TEST_F(ArmyExchangeExecutionTest, StaleDestinationCreatureStopsBeforeAnyRequest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto plan = partialThenWhole();
	plan.transfers[0].expectedDestinationCreature = pikeman;
	plan.transfers[0].expectedDestinationCount = 1;
	plan.transfers[0].resultingDestinationCount = 6;
	ASSERT_TRUE(receiver->setCreature(SlotID(0), guard, 1));
	const auto before = totals();
	execute(plan);
	EXPECT_TRUE(client->requests.empty());
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, MatchingButDifferentCreatureDestinationCannotTurnPartialTransferIntoSwap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto plan = partialThenWhole();
	plan.transfers[0].expectedDestinationCreature = guard;
	plan.transfers[0].expectedDestinationCount = 1;
	plan.transfers[0].resultingDestinationCount = 6;
	ASSERT_TRUE(receiver->setCreature(SlotID(0), guard, 1));
	const auto heroBefore = snapshot(receiver), townBefore = snapshot(donor);
	execute(plan);
	EXPECT_TRUE(client->requests.empty());
	EXPECT_EQ(snapshot(receiver), heroBefore);
	EXPECT_EQ(snapshot(donor), townBefore);
}

TEST_F(ArmyExchangeExecutionTest, LaterStaleSlotStopsWithoutReplanningOrUndoingAcceptedPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = totals();
	client->afterApplied = [this]()
	{
		donor->putStack(SlotID(3), donor->detachStack(SlotID(2)));
	};
	execute(partialThenWhole());
	ASSERT_EQ(client->requests.size(), 1);
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(receiver->getStackPtr(SlotID(2)), nullptr);
	EXPECT_EQ(donor->getStackCount(SlotID(3)), 4);
	EXPECT_EQ(totals(), before);
}

TEST_F(ArmyExchangeExecutionTest, AuthoritativeOwnershipRejectionStopsWithoutTryingLaterTransfers)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Stack preconditions still match; ownership rejection remains the server's decision.
	donor->tempOwner = PlayerColor(1);
	const auto heroBefore = snapshot(receiver), townBefore = snapshot(donor);
	execute(partialThenWhole());
	ASSERT_EQ(client->requests.size(), 1);
	EXPECT_EQ(snapshot(receiver), heroBefore);
	EXPECT_EQ(snapshot(donor), townBefore);
}

TEST_F(ArmyExchangeExecutionTest, ThrowingSubmissionRestoresPriorWaitMode)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	client->throwOnRequest = true;
	const auto heroBefore = snapshot(receiver), townBefore = snapshot(donor);
	EXPECT_THROW(executeArmyExchange(callback, receiver, donor, partialThenWhole()), std::runtime_error);
	EXPECT_FALSE(callback->waitTillRealize);
	ASSERT_EQ(client->waits.size(), 1);
	EXPECT_TRUE(client->waits.front());
	EXPECT_EQ(snapshot(receiver), heroBefore);
	EXPECT_EQ(snapshot(donor), townBefore);
}

TEST_F(ArmyExchangeExecutionTest, EmptyProjectionRestoresAnAlreadySynchronousCaller)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->waitTillRealize = true;
	const auto heroBefore = snapshot(receiver), townBefore = snapshot(donor);
	executeArmyExchange(callback, receiver, donor, {});
	EXPECT_TRUE(callback->waitTillRealize);
	EXPECT_TRUE(client->requests.empty());
	EXPECT_EQ(snapshot(receiver), heroBefore);
	EXPECT_EQ(snapshot(donor), townBefore);
}
