/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Helpers/ArmyUpgradeSelection.h"
#include "lib/callback/IClient.h"
#include "lib/CPlayerState.h"
#include "lib/GameLibrary.h"
#include "lib/GameConstants.h"
#include "lib/gameState/UpgradeInfo.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"
namespace
{
class UpgradeClient final : public IClient
{
	GameHandlerTestServer server;
	CGameHandler handler;
public:
	int requests = 0;
	bool accept = true;
	explicit UpgradeClient(const std::shared_ptr<CGameState> & state)
		: server(state, PlayerColor(0)), handler(server, state)
	{
		state->actingPlayers.insert(PlayerColor(0));
	}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		request.player = player;
		request.requestID = ++requests;
		if(!accept)
			return requests;
		auto copy = CMemorySerializer::deepCopy(request);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *copy);
		return requests;
	}
};
class NewHorizonsGhostUpgradeAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CreatureID wraith() const { return CreatureID(CreatureID::decode("core:wraith")); }
	CreatureID ghost() const { return CreatureID(CreatureID::decode("new-horizons:ghost")); }
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void prepare(bool vidomina = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(vidomina ? "core:vidomina" : "core:charna")), PlayerColor(0))
			.town({12, 12, 0}, FactionID::NECROPOLIS, PlayerColor(0))
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		hero->clearSlots();
		ASSERT_TRUE(hero->setCreature(SlotID(0), wraith(), 1));
		gameState()->getPlayerState(PlayerColor(0))->resources = TResources();
		revealMap(PlayerColor(0));
	}
	UpgradeInfo offer()
	{
		UpgradeInfo result(wraith());
		gameState()->fillUpgradeInfo(hero, SlotID(0), result);
		return result;
	}
};
}
TEST_F(NewHorizonsGhostUpgradeAITest, ImmediateAIExecutesOneRecognizedFreeUpgradeWithoutInventedArmyValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(wraith().toCreature()->getAIValue(), ghost().toCreature()->getAIValue());
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	UpgradeClient client(gameState());
	auto gateway = makeGateway(PlayerColor(0), &client);
	EXPECT_TRUE(gateway->makePossibleUpgrades(hero));
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), ghost());
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_EQ(client.requests, 1);
	EXPECT_FALSE(gateway->makePossibleUpgrades(hero));
	EXPECT_EQ(client.requests, 1);
}
TEST_F(NewHorizonsGhostUpgradeAITest, ChoiceRequiresActualOfferAndQualifiedCarrier)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto offered = offer();
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, TResources()));
	offered.addUpgrade(ghost(), wraith().toCreature());
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, TResources()));
}
TEST_F(NewHorizonsGhostUpgradeAITest, PaidEqualValueGhostOfferIsNotGivenAnInventedBenefit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	UpgradeInfo offered(wraith());
	offered.addUpgrade(ghost(), CreatureID(CreatureID::SKELETON).toCreature());
	TResources available;
	available[EGameResID::GOLD] = 100000;
	ASSERT_NE(offered.getUpgradeCostsFor(ghost()), TResources());
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, available));
}
TEST_F(NewHorizonsGhostUpgradeAITest, GenericEqualValueOfferRemainsUnselected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	UpgradeInfo offered(wraith());
	offered.addUpgrade(wraith(), wraith().toCreature());
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, TResources()));
}
TEST_F(NewHorizonsGhostUpgradeAITest, OrdinaryPaidUpgradeKeepsExistingValueAndAffordabilityPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const CreatureID pikeman(CreatureID::decode("core:pikeman")), halberdier(CreatureID::decode("core:halberdier"));
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 1));
	UpgradeInfo offered(pikeman);
	offered.addUpgrade(halberdier, pikeman.toCreature());
	const auto & stack = *hero->getStackPtr(SlotID(0));
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(stack, offered, hero, TResources()));
	EXPECT_EQ(NK2AI::armyUpgrade::choose(stack, offered, hero, offered.getUpgradeCostsFor(halberdier)), halberdier);
}

TEST_F(NewHorizonsGhostUpgradeAITest, TurnPreparationUpgradesFieldVidominaBeforePlanning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(hero->getVisitedTown(), nullptr);
	UpgradeClient client(gameState());
	auto gateway = makeGateway(PlayerColor(0), &client);
	gateway->upgradeGhosts();
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), ghost());
	EXPECT_EQ(client.requests, 1);
	gateway->upgradeGhosts();
	EXPECT_EQ(client.requests, 1);
}
TEST_F(NewHorizonsGhostUpgradeAITest, TurnPreparationLeavesUnqualifiedFieldHeroUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_EQ(hero->getVisitedTown(), nullptr);
	UpgradeClient client(gameState());
	auto gateway = makeGateway(PlayerColor(0), &client);
	gateway->upgradeGhosts();
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), wraith());
	EXPECT_EQ(client.requests, 0);
}
TEST_F(NewHorizonsGhostUpgradeAITest, RejectedOrUnobservedImmediateRequestExitsAfterOneAttempt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	UpgradeClient client(gameState());
	client.accept = false;
	auto gateway = makeGateway(PlayerColor(0), &client);
	EXPECT_FALSE(gateway->makePossibleUpgrades(hero));
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), wraith());
	EXPECT_EQ(client.requests, 1);
}
TEST_F(NewHorizonsGhostUpgradeAITest, UnaffordableHighestValueOfferRetainsExistingNoFallbackPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto offered = offer();
	const CreatureID stronger(CreatureID::decode("core:boneDragon"));
	ASSERT_GT(stronger.toCreature()->getAIValue(), ghost().toCreature()->getAIValue());
	ASSERT_TRUE(hero->getLeadershipSlotCapacity(stronger)->accepts(1));
	offered.addUpgrade(stronger, wraith().toCreature());
	ASSERT_NE(offered.getUpgradeCostsFor(stronger), TResources());
	EXPECT_FALSE(NK2AI::armyUpgrade::choose(*hero->getStackPtr(SlotID(0)), offered, hero, TResources()));
}
