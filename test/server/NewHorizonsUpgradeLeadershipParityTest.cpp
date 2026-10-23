/*
 * NewHorizonsUpgradeLeadershipParityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/GameStatistics.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../server/CGameHandler.h"

namespace
{
class UpgradeAdmissionServer : public GameHandlerTestServer
{
public:
	explicit UpgradeAdmissionServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PlayerColor(0))
	{}
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<SetStackType *>(&pack))
			++stackTypePacks;
		if(dynamic_cast<SetResources *>(&pack))
			++resourcePacks;
		GameHandlerTestServer::applyPack(pack);
	}
	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		if(const auto * applied = dynamic_cast<PackageApplied *>(&pack))
			responses.push_back(*applied);
		GameHandlerTestServer::sendPack(pack, connection);
	}
	std::vector<PackageApplied> responses;
	int stackTypePacks = 0;
	int resourcePacks = 0;
};

class NewHorizonsUpgradeLeadershipParityTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<bool>
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
		if(GetParam())
		{
			rules["leadership"]["upgradeMultiplierPercent"].Integer() = 120;
			rules["leadership"]["upgradeRounding"].Integer() = 10;
			auto & requirements = rules["leadership"]["creatureRequirements"];
			requirements["core:sprite"].Integer() = 60;
			requirements["new-horizons:wispUpgrade"].Integer() = 170;
			requirements.Struct().erase("core:firebird");
			requirements.Struct().erase("core:psychicElemental");
			// This is a whole historical capture, not a patch merged over today's table.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
		const auto & effective = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES);
		EXPECT_EQ(effective["leadership"]["creatureRequirements"].Struct().contains("core:firebird"), !GetParam());
		EXPECT_EQ(effective["leadership"]["creatureRequirements"].Struct().contains("core:psychicElemental"), !GetParam());
	}

	void startScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0))
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
	}
};
}

TEST_P(NewHorizonsUpgradeLeadershipParityTest, WorldAndHeroRetainCapturedUpgradeRulesAcrossSaveLoad)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto worldRules = gameState()->getHeroCapabilityRules();
	const auto heroRules = hero->getCapabilityRules();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getMap().getHero(hero->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(restored.getHeroCapabilityRules(), worldRules);
	EXPECT_EQ(loaded->getCapabilityRules(), heroRules);
	const auto capacity = loaded->getLeadershipSlotCapacity(CreatureID(CreatureID::decode("core:halberdier")));
	ASSERT_TRUE(capacity);
	EXPECT_EQ(capacity->requirement, GetParam() ? 70 : 60);
	EXPECT_EQ(worldRules["leadership"]["upgradeMultiplierPercent"].Integer(), GetParam() ? 120 : 100);
	EXPECT_EQ(worldRules["leadership"]["upgradeRounding"].Integer(), GetParam() ? 10 : 1);
	for(const auto * captured : {&worldRules, &heroRules,
		&restored.getHeroCapabilityRules(), &loaded->getCapabilityRules()})
	{
		const auto & requirements = (*captured)["leadership"]["creatureRequirements"];
		EXPECT_EQ(requirements["core:sprite"].Integer(), GetParam() ? 60 : 45);
		EXPECT_EQ(requirements["new-horizons:wispUpgrade"].Integer(), GetParam() ? 170 : 140);
		EXPECT_EQ(requirements.Struct().contains("core:firebird"), !GetParam());
		EXPECT_EQ(requirements.Struct().contains("core:psychicElemental"), !GetParam());
	}
	for(const char * identifier : {"core:firebird", "core:psychicElemental"})
	{
		SCOPED_TRACE(identifier);
		const auto legacyCapacity = loaded->getLeadershipSlotCapacity(CreatureID(CreatureID::decode(identifier)));
		EXPECT_EQ(legacyCapacity.has_value(), !GetParam());
	}
	for(const auto & [identifier, expected] : {
		std::pair{"core:sprite", GetParam() ? 60 : 45},
		{"new-horizons:wispUpgrade", GetParam() ? 170 : 140}})
	{
		SCOPED_TRACE(identifier);
		const auto explicitCapacity = loaded->getLeadershipSlotCapacity(CreatureID(CreatureID::decode(identifier)));
		ASSERT_TRUE(explicitCapacity);
		EXPECT_EQ(explicitCapacity->requirement, expected);
	}
}

TEST_P(NewHorizonsUpgradeLeadershipParityTest, AuthoritativeUpgradeUsesCapturedCapacityWithoutChangingStackCount)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const PlayerColor player(0);
	auto * hero = findHeroByOwner(player);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	const CreatureID base(CreatureID::decode("core:pikeman"));
	const CreatureID upgrade(CreatureID::decode("core:halberdier"));
	const auto before = hero->getLeadershipSlotCapacity(base);
	const auto after = hero->getLeadershipSlotCapacity(upgrade);
	ASSERT_TRUE(before);
	ASSERT_TRUE(after);
	const int count = before->maximum;
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), base, count));
	town->addBuilding(BuildingID::getDwellingFromLevel(0, 0));
	town->addBuilding(BuildingID::getDwellingFromLevel(0, 1));
	town->creatures = {{0, {base, upgrade}}};
	town->setVisitingHero(hero);
	for(size_t resource = 0; resource < GameConstants::RESOURCE_QUANTITY; ++resource)
		gameState()->getPlayerState(player)->resources[resource] = 100000;
	const auto resourcesBefore = gameState()->getPlayerState(player)->resources;
	UpgradeInfo info(base);
	gameState()->fillUpgradeInfo(hero, SlotID(0), info);
	ASSERT_TRUE(vstd::contains(info.getAvailableUpgrades(), upgrade));
	gameState()->actingPlayers.insert(player);
	GameHandlerTestServer server(gameState(), player);
	CGameHandler handler(server, gameState());
	UpgradeCreature request(SlotID(0), hero->id, upgrade);
	request.player = player;
	request.requestID = 742;
	handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), count);
	if(GetParam())
	{
		ASSERT_GT(count, after->maximum);
		EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), base);
		EXPECT_EQ(gameState()->getPlayerState(player)->resources, resourcesBefore);
	}
	else
	{
		EXPECT_EQ(after->maximum, before->maximum);
		EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), upgrade);
		EXPECT_EQ(int64_t(count) * before->requirement, int64_t(count) * after->requirement);
		EXPECT_EQ(gameState()->getPlayerState(player)->resources,
			resourcesBefore - info.getUpgradeCostsFor(upgrade) * count);
	}
}

TEST_P(NewHorizonsUpgradeLeadershipParityTest, UnofferedUpgradeRequestsRejectAtomicallyBeforeCostLookup)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const PlayerColor player(0);
	auto * hero = findHeroByOwner(player);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	const CreatureID base(CreatureID::decode("core:pikeman"));
	const CreatureID upgrade(CreatureID::decode("core:halberdier"));
	const CreatureID unrelated(CreatureID::decode("core:archer"));
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), base, 1));
	town->addBuilding(BuildingID::getDwellingFromLevel(0, 0));
	town->addBuilding(BuildingID::getDwellingFromLevel(0, 1));
	town->creatures = {{9, {base, upgrade}}};
	town->setVisitingHero(hero);
	for(size_t resource = 0; resource < GameConstants::RESOURCE_QUANTITY; ++resource)
		gameState()->getPlayerState(player)->resources[resource] = 100000;
	gameState()->actingPlayers.insert(player);
	UpgradeInfo info(base);
	gameState()->fillUpgradeInfo(hero, SlotID(0), info);
	ASSERT_TRUE(vstd::contains(info.getAvailableUpgrades(), upgrade));
	const auto resources = gameState()->getPlayerState(player)->resources;
	const auto stock = town->creatures;
	UpgradeAdmissionServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto spending = handler.statistics->getPlayerAccumulator(player).spentResourcesForArmy;
	for(const auto target : {base, unrelated})
	{
		SCOPED_TRACE(CreatureID::encode(target.getNum()));
		ASSERT_FALSE(vstd::contains(info.getAvailableUpgrades(), target));
		const auto capacity = hero->getLeadershipSlotCapacity(target);
		ASSERT_TRUE(capacity);
		ASSERT_GE(capacity->maximum, 1); // Leadership must not mask target rejection.
		UpgradeCreature request(SlotID(0), hero->id, target);
		request.player = player;
		request.requestID = 744 + server.responses.size();
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
		ASSERT_FALSE(server.responses.empty());
		EXPECT_EQ(server.responses.back().requestID, request.requestID);
		EXPECT_FALSE(server.responses.back().result);
		EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), base);
		EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
		EXPECT_EQ(gameState()->getPlayerState(player)->resources, resources);
		EXPECT_EQ(handler.statistics->getPlayerAccumulator(player).spentResourcesForArmy, spending);
		EXPECT_EQ(town->creatures, stock);
		EXPECT_EQ(server.stackTypePacks, 0);
		EXPECT_EQ(server.resourcePacks, 0);
	}
	UpgradeCreature valid(SlotID(0), hero->id, upgrade);
	valid.player = player;
	valid.requestID = 746;
	handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, valid);
	ASSERT_EQ(server.responses.size(), 3u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), upgrade);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	const auto price = info.getUpgradeCostsFor(upgrade);
	EXPECT_EQ(gameState()->getPlayerState(player)->resources, resources - price);
	EXPECT_EQ(handler.statistics->getPlayerAccumulator(player).spentResourcesForArmy, spending + price);
	EXPECT_EQ(town->creatures, stock);
	EXPECT_EQ(server.stackTypePacks, 1);
	EXPECT_EQ(server.resourcePacks, 1);
}

INSTANTIATE_TEST_SUITE_P(CurrentAndHistorical, NewHorizonsUpgradeLeadershipParityTest, ::testing::Bool());
