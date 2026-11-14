/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Helpers/ArmyUpgradeSelection.h"
#include "lib/callback/IClient.h"
#include "lib/CPlayerState.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/gameState/UpgradeInfo.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapping/CMap.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"
namespace
{
class GoldUpgradeClient final : public IClient
{
	GameHandlerTestServer server;
	CGameHandler handler;
public:
	int requests = 0;
	explicit GoldUpgradeClient(const std::shared_ptr<CGameState> & state)
		: server(state, PlayerColor(0)), handler(server, state) { state->actingPlayers.insert(PlayerColor(0)); }
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		request.player = player; request.requestID = ++requests;
		auto copy = CMemorySerializer::deepCopy(request);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *copy);
		return requests;
	}
};
class NewHorizonsGoldStandardAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	bool capturedGoldRequirement = false;
	const CreatureID iron{CreatureID::decode("core:stoneGolem")}; // Canonical content key for actual Iron Golem33.
	const CreatureID stone{CreatureID::decode("core:ironGolem")}; // Actual Stone Golem32.
	const CreatureID gold{CreatureID::decode("core:goldGolem")};
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		NullkillerTest::mapLoaded(map);
		if(capturedGoldRequirement)
		{
			JsonNode policy(JsonPath::builtin("config/newHorizonsCapabilities"));
			policy["leadership"]["creatureRequirements"]["core:goldGolem"].Integer() = 400;
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, policy);
		}
	}
	void prepare(const char * identity = "core:josephine")
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(identity)), PlayerColor(0)).heroExperience(0)
			.hero({25, 25, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0)); ASSERT_NE(hero, nullptr);
		hero->clearSlots(); ASSERT_TRUE(hero->setCreature(SlotID(0), iron, 1));
		for(int slot = 1; slot < GameConstants::ARMY_SIZE; ++slot)
			ASSERT_TRUE(hero->setCreature(SlotID(slot), CreatureID(CreatureID::decode("core:pikeman")), 1));
		for(size_t resource = 0; resource < GameConstants::RESOURCE_QUANTITY; ++resource)
			gameState()->getPlayerState(PlayerColor(0))->resources[resource] = 100000;
		revealMap(PlayerColor(0));
	}
	UpgradeInfo offers() const
	{
		UpgradeInfo result(hero->getStackPtr(SlotID(0))->getId());
		gameState()->fillUpgradeInfo(hero, SlotID(0), result);
		return result;
	}
};
}
TEST_F(NewHorizonsGoldStandardAITest, GenericGatewayExecutesActualPaidIronToGoldOfferWithAllSevenSlotsOccupied)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(iron.getNum(), 33); ASSERT_EQ(gold.getNum(), 116);
	const auto offered = offers();
	ASSERT_TRUE(vstd::contains(offered.getAvailableUpgrades(), gold));
	ASSERT_GT(gold.toCreature()->getAIValue(), iron.toCreature()->getAIValue());
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto price = offered.getUpgradeCostsFor(gold);
	ASSERT_NE(price, TResources());
	GoldUpgradeClient client(gameState()); auto gateway = makeGateway(PlayerColor(0), &client);
	ASSERT_TRUE(gateway->makePossibleUpgrades(hero));
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getId(), gold);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before - price);
	EXPECT_EQ(client.requests, 1);
}
TEST_F(NewHorizonsGoldStandardAITest, ActualOffersAndFundsGateTheExistingChoiceWithoutInventingAnUpgrade)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto offered = offers();
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, TResources()));
	ASSERT_TRUE(hero->setCreature(SlotID(0), stone, 1));
	const auto wrongFamily = offers();
	EXPECT_FALSE(vstd::contains(wrongFamily.getAvailableUpgrades(), gold));
}
TEST_F(NewHorizonsGoldStandardAITest, CurrentV7CapturedRequirementChangesEfficiencyWithoutBlockingPaidUpgrade)
{
	capturedGoldRequirement = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(hero->getCapabilityRules()["rulesetVersion"].Integer(), 7);
	EXPECT_FALSE(hero->getLeadershipSlotCapacity(gold));
	const auto threshold = newHorizonsHeroes::stackFullEfficiencyCapacity(hero, gold);
	ASSERT_GE(threshold, 1);
	const int count = threshold + 1;
	ASSERT_TRUE(hero->setCreature(SlotID(0), iron, count));
	const auto available = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_EQ(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offers(), hero, available), gold);
	const auto price = offers().getUpgradeCostsFor(gold) * count;
	ASSERT_TRUE(available.canAfford(price));
	GoldUpgradeClient client(gameState()); auto gateway = makeGateway(PlayerColor(0), &client);
	ASSERT_TRUE(gateway->makePossibleUpgrades(hero));
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getId(), gold);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), count);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, available - price);
	EXPECT_EQ(client.requests, 1);
}
