/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/gameState/GameStatistics.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../server/CGameHandler.h"
namespace
{
class GoldStandardServer : public GameHandlerTestServer
{
public:
	GoldStandardServer(std::shared_ptr<CGameState> state, PlayerColor owner)
		: GameHandlerTestServer(std::move(state), owner) {}
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<SetStackType *>(&pack))
			++types;
		if(dynamic_cast<SetResources *>(&pack))
			++resources;
		GameHandlerTestServer::applyPack(pack);
	}
	int types = 0;
	int resources = 0;
};
class GoldStandardUpgradeTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * hero = nullptr;
	bool explicitGoldLeadership = false;
	const CreatureID iron{CreatureID::decode("core:stoneGolem")};
	const CreatureID stone{CreatureID::decode("core:ironGolem")};
	const CreatureID gold{CreatureID::decode("core:goldGolem")};
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		if(explicitGoldLeadership)
		{
			JsonNode policy(JsonPath::builtin("config/newHorizonsCapabilities"));
			// A captured map policy exercises the existing admission, without inventing
			// a default Gold Golem balance requirement absent from the authored table.
			policy["leadership"]["creatureRequirements"]["core:goldGolem"].Integer() = 400;
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, policy);
		}
	}
	void prepare(const char * identity = "core:josephine", int count = 1)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(identity)), PlayerColor(0)).heroExperience(0)
			.hero({25, 25, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		hero->clearSlots();
		ASSERT_TRUE(hero->setCreature(SlotID(0), iron, count));
		for(int slot = 1; slot < GameConstants::ARMY_SIZE; ++slot)
			ASSERT_TRUE(hero->setCreature(SlotID(slot), CreatureID(CreatureID::decode("core:pikeman")), 1));
		for(size_t resource = 0; resource < GameConstants::RESOURCE_QUANTITY; ++resource)
			gameState()->getPlayerState(PlayerColor(0))->resources[resource] = 100000;
	}
	UpgradeInfo offered() const
	{
		UpgradeInfo result(iron);
		gameState()->fillUpgradeInfo(hero, SlotID(0), result);
		return result;
	}
	void request(CGameHandler & handler, PlayerColor sender = PlayerColor(0), int requestID = 805)
	{
		gameState()->actingPlayers.insert(sender);
		UpgradeCreature pack(SlotID(0), hero->id, gold);
		pack.player = sender;
		pack.requestID = requestID;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, pack);
	}
};
}
TEST_F(GoldStandardUpgradeTest, ActualIronOnlyUsesOrdinaryPaidUpgradeWithSevenOccupiedSlots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(iron.getNum(), 33);
	EXPECT_EQ(stone.getNum(), 32);
	EXPECT_EQ(gold.getNum(), 116);
	const auto info = offered();
	ASSERT_TRUE(vstd::contains(info.getAvailableUpgrades(), gold));
	ResourceSet expected = gold.toCreature()->getFullRecruitCost() - iron.toCreature()->getFullRecruitCost();
	expected.positive();
	EXPECT_EQ(info.getUpgradeCostsFor(gold), expected);
	ASSERT_TRUE(hero->setCreature(SlotID(1), stone, 1));
	UpgradeInfo baseInfo(stone);
	gameState()->fillUpgradeInfo(hero, SlotID(1), baseInfo);
	EXPECT_FALSE(vstd::contains(baseInfo.getAvailableUpgrades(), gold));
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto movement = hero->movementPointsRemaining();
	GoldStandardServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	request(handler);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), gold);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(hero->Slots().size(), GameConstants::ARMY_SIZE);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before - expected);
	EXPECT_EQ(server.types, 1);
	EXPECT_EQ(server.resources, 1);
}
TEST_F(GoldStandardUpgradeTest, OtherHeroCannotBuyUnOfferedGoldUpgrade)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:neela"));
	EXPECT_FALSE(vstd::contains(offered().getAvailableUpgrades(), gold));
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	GoldStandardServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	request(handler);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), iron);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_EQ(server.types + server.resources, 0);
}
TEST_F(GoldStandardUpgradeTest, WrongConnectionOwnerCannotUpgradeAnOtherwiseEligibleArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(vstd::contains(offered().getAvailableUpgrades(), gold));
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	GoldStandardServer server(gameState(), PlayerColor(1));
	CGameHandler handler(server, gameState());
	request(handler, PlayerColor(1));
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), iron);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_EQ(server.types + server.resources, 0);
	EXPECT_TRUE(handler.upgradeCreature(hero->id, SlotID(0), gold));
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), gold);
}
TEST_F(GoldStandardUpgradeTest, ExactPaymentBoundaryRejectsAtomicallyThenChargesOrdinaryCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto price = offered().getUpgradeCostsFor(gold);
	ASSERT_GT(price[GameResID::GOLD], 0);
	auto & resources = gameState()->getPlayerState(PlayerColor(0))->resources;
	resources = price;
	--resources[GameResID::GOLD];
	const auto before = resources;
	GoldStandardServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	request(handler);
	EXPECT_EQ(resources, before);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), iron);
	EXPECT_EQ(server.types + server.resources, 0);
	++resources[GameResID::GOLD];
	request(handler, PlayerColor(0), 806);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), gold);
	EXPECT_EQ(resources, ResourceSet());
	EXPECT_EQ(server.types, 1);
	EXPECT_EQ(server.resources, 1);
}
TEST_F(GoldStandardUpgradeTest, CapturedGoldLeadershipRejectsBeforePaymentAndAcceptsAtCapacity)
{
	explicitGoldLeadership = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto source = hero->getLeadershipSlotCapacity(iron);
	const auto target = hero->getLeadershipSlotCapacity(gold);
	ASSERT_TRUE(source);
	ASSERT_TRUE(target);
	ASSERT_GE(source->maximum, target->maximum + 1);
	ASSERT_GE(target->maximum, 1);
	ASSERT_TRUE(hero->setCreature(SlotID(0), iron, target->maximum + 1));
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	GoldStandardServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	request(handler);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), iron);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_EQ(server.types + server.resources, 0);
	ASSERT_TRUE(hero->setCreature(SlotID(0), iron, target->maximum));
	const auto cost = offered().getUpgradeCostsFor(gold) * target->maximum;
	request(handler, PlayerColor(0), 806);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), gold);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), target->maximum);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before - cost);
}
TEST_F(GoldStandardUpgradeTest, SaveLoadRetainsCapturedCapabilityAndOrdinaryOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getMap().getHero(hero->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), hero->getPrimaryGrowthRules());
	UpgradeInfo info(iron);
	restored.fillUpgradeInfo(loaded, SlotID(0), info);
	EXPECT_TRUE(vstd::contains(info.getAvailableUpgrades(), gold));
	EXPECT_EQ(loaded->getCreature(SlotID(0))->getId(), iron);
}
