/*
 * BuyArmyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Goals/BuyArmy.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/TaskExecution.h"
#include "nullkiller2/NullkillerTest.h"
#include "mock/GameHandlerTestServer.h"
#include "lib/CPlayerState.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"

#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include <tuple>

namespace
{
void fillFullArmy(CCreatureSet & army)
{
	const std::array<CreatureID, GameConstants::ARMY_SIZE> creatures = {
		CreatureID(CreatureID::ARCHER),
		CreatureID(0),
		CreatureID(1),
		CreatureID(4),
		CreatureID(5),
		CreatureID(6),
		CreatureID(7)
	};

	for(size_t index = 0; index < creatures.size(); ++index)
		ASSERT_TRUE(army.setCreature(SlotID(index), creatures[index], 1));
}
}

TEST(Nullkiller2_Goals_BuyArmy, fullArmyDoesNotNeedFreeSlotForExistingCreature)
{
	CGHeroInstance army(nullptr);
	fillFullArmy(army);

	ASSERT_EQ(army.stacksCount(), GameConstants::ARMY_SIZE);

	EXPECT_FALSE(NK2AI::Goals::BuyArmy::needsFreeSlotToRecruit(
		&army,
		CreatureID(CreatureID::ARCHER)))
		<< "recruiting an existing creature type can merge without dismissing a stack";

	EXPECT_TRUE(NK2AI::Goals::BuyArmy::needsFreeSlotToRecruit(
		&army,
		CreatureID(8)))
		<< "a full army still needs a free slot for a missing creature type";
}

TEST(Nullkiller2_Goals_BuyArmy, partialArmyDoesNotNeedFreeSlotForMissingCreature)
{
	CGHeroInstance army(nullptr);
	ASSERT_TRUE(army.setCreature(SlotID(0), CreatureID(CreatureID::ARCHER), 1));

	EXPECT_FALSE(NK2AI::Goals::BuyArmy::needsFreeSlotToRecruit(
		&army,
		CreatureID(8)));
}

namespace
{
const PlayerColor BUYER(0);

// Only the forecast boundary is injected. All final admission and requests use
// the real BuyArmy, callback and authoritative game handler below.
class CandidateArmyManager final : public NK2AI::ArmyManager
{
public:
	std::vector<NK2AI::creInfo> candidates;
	mutable unsigned forecasts = 0;
	CandidateArmyManager(CPlayerSpecificInfoCallback * callback, const NK2AI::Nullkiller * ai)
		: ArmyManager(callback, ai) {}
	std::vector<NK2AI::creInfo> getArmyAvailableToBuy(const CCreatureSet *, const CGDwelling *) const override
	{
		++forecasts;
		return candidates;
	}
};

class PurchaseClient final : public IClient
{
	GameHandlerTestServer server;
	CGameHandler handler;
public:
	unsigned requests = 0;
	std::vector<RecruitCreatures> recruits;
	unsigned upgrades = 0;
	explicit PurchaseClient(const std::shared_ptr<CGameState> & state)
		: server(state, BUYER), handler(server, state)
	{
		state->actingPlayers.insert(BUYER);
	}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = static_cast<int>(++requests);
		if(const auto * recruit = dynamic_cast<const RecruitCreatures *>(incoming.get()))
			recruits.push_back(*recruit);
		if(dynamic_cast<const UpgradeCreature *>(incoming.get()))
			++upgrades;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return incoming->requestID;
	}
};

class Nullkiller2BuyArmyExecutionTest : public NullkillerTest
{
protected:
	CGTownInstance * town = nullptr;
	CreatureID pikeman;
	std::unique_ptr<PurchaseClient> client;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	CandidateArmyManager * manager = nullptr;

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(BUYER)
			.town({10, 10, 0}, FactionID::CASTLE, BUYER)
			.hero({6, 10, 0}, HeroTypeID(0), BUYER);
		startWithMap(std::move(builder));
		town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		town->clearSlots();
		for(int level = 0; level < 7; ++level)
			town->removeBuilding(BuildingID::getDwellingFromLevel(level, 1));
		town->addBuilding(BuildingID::getDwellingFromLevel(0, 0));
		pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		town->creatures = {{10, {pikeman}}};
		gameState()->getPlayerState(BUYER)->resources = ResourceSet();
		gameState()->getPlayerState(BUYER)->resources[GameResID(EGameResID::GOLD)] = 100000;
		client = std::make_unique<PurchaseClient>(gameState());
		gateway = makeGateway(BUYER, client.get());
		auto injected = std::make_unique<CandidateArmyManager>(gateway->cc.get(), gateway->nullkiller.get());
		manager = injected.get();
		gateway->nullkiller->armyManager = std::move(injected);
	}

	NK2AI::TaskExecutionResult execute()
	{
		return NK2AI::executeTask(*gateway,
			NK2AI::Goals::taskptr(NK2AI::Goals::BuyArmy(town, std::numeric_limits<int>::max())));
	}

	auto army() const
	{
		std::vector<std::tuple<SlotID, CreatureID, TQuantity>> result;
		for(const auto & [slot, stack] : town->Slots())
			result.emplace_back(slot, stack->getCreatureID(), stack->getCount());
		return result;
	}
};
}

TEST_F(Nullkiller2BuyArmyExecutionTest, BlockedPositiveCandidateWithoutRequestFails)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Explicit stale forecast: seven own-faction types cannot be dismissed.
	for(int index = 0; index < GameConstants::ARMY_SIZE; ++index)
	{
		const CreatureID creature(index);
		ASSERT_EQ(creature.toCreature()->getFactionID(), town->getFactionID());
		ASSERT_TRUE(town->setCreature(SlotID(index), creature, 1));
	}
	const CreatureID missing(8);
	ASSERT_EQ(missing.toCreature()->getFactionID(), town->getFactionID());
	ASSERT_FALSE(town->getSlotFor(missing).validSlot());
	town->creatures = {{1, {missing}}};
	manager->candidates = {{1, missing, 0}};
	const auto before = army();
	const auto resources = gameState()->getPlayerState(BUYER)->resources;
	const auto stock = town->creatures;
	const auto result = execute();
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.unavailableRouteHero);
	EXPECT_EQ(manager->forecasts, 1u);
	EXPECT_EQ(client->requests, 0u);
	EXPECT_EQ(army(), before);
	EXPECT_EQ(town->creatures, stock);
	EXPECT_EQ(gameState()->getPlayerState(BUYER)->resources, resources);
}

TEST_F(Nullkiller2BuyArmyExecutionTest, LegalCandidateSubmitsAndPaysForActualQuantity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	manager->candidates = {{3, pikeman, 0}};
	const auto cost = town->getRecruitmentCost(pikeman);
	const auto resources = gameState()->getPlayerState(BUYER)->resources;
	EXPECT_TRUE(execute().success);
	ASSERT_EQ(client->recruits.size(), 1u);
	EXPECT_EQ(client->recruits.front().amount, 3u);
	EXPECT_EQ(town->getStackCount(town->getSlotFor(pikeman)), 3);
	EXPECT_EQ(town->creatures.front().first, 7u);
	EXPECT_EQ(gameState()->getPlayerState(BUYER)->resources, resources - cost * 3);
}

TEST_F(Nullkiller2BuyArmyExecutionTest, UnaffordableCandidateMakesNoRequestAndFails)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	gameState()->getPlayerState(BUYER)->resources = ResourceSet();
	manager->candidates = {{3, pikeman, 0}};
	EXPECT_FALSE(execute().success);
	EXPECT_EQ(client->requests, 0u);
	EXPECT_EQ(town->stacksCount(), 0u);
	EXPECT_EQ(town->creatures.front().first, 10u);
}

TEST_F(Nullkiller2BuyArmyExecutionTest, UpgradeOnlySuccessDoesNotRequireRecruitment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const CreatureID upgraded(CreatureID::decode("core:halberdier"));
	ASSERT_TRUE(town->setCreature(SlotID(0), pikeman, 1));
	ASSERT_GT(upgraded.toCreature()->getAIValue(), pikeman.toCreature()->getAIValue());
	town->addBuilding(BuildingID::getDwellingFromLevel(0, 1));
	town->creatures = {{0, {pikeman, upgraded}}};
	manager->candidates.clear();
	EXPECT_TRUE(execute().success);
	EXPECT_EQ(client->upgrades, 1u);
	EXPECT_TRUE(client->recruits.empty());
	EXPECT_EQ(town->getStackPtr(SlotID(0))->getCreatureID(), upgraded);
}
