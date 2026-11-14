/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Helpers/RecruitmentRowPlan.h"
#include "lib/CPlayerState.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/mapObjects/NewHorizonsEstates.h"
#include "lib/mapObjects/TownRecruitmentOffers.h"
#include "lib/modding/CModHandler.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"
#include "server/battles/FullGameSnapshotTypes.h"
#include <array>

namespace
{
const PlayerColor PLAYER(0);
CreatureID creature(const char * key) { return CreatureID(CreatureID::decode(key)); }

// Same loopback contract as the existing BuyArmy execution fixture: real AI
// requests pass through the normal callback and authoritative pack dispatcher.
class SethRecruitmentClient final : public IClient
{
	GameHandlerTestServer server;
	CGameHandler handler;
	int sequence = 0;
public:
	std::vector<RecruitCreatures> requests;
	explicit SethRecruitmentClient(const std::shared_ptr<CGameState> & state)
		: server(state, PLAYER), handler(server, state) { state->actingPlayers.insert(PLAYER); }
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto pack = CMemorySerializer::deepCopy(request);
		pack->player = player;
		pack->requestID = ++sequence;
		if(const auto * recruit = dynamic_cast<const RecruitCreatures *>(pack.get())) requests.push_back(*recruit);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *pack);
		return sequence;
	}
};

class NewHorizonsSethRecruitmentAITest : public NullkillerTest
{
protected:
	CGTownInstance * town = nullptr;
	CGHeroInstance * seth = nullptr;
	CGHeroInstance * buyer = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(50));
	}
	void prepare(bool upgraded = false)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.town({10, 10, 0}, FactionID::NECROPOLIS, PLAYER).townGarrison({})
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:nimbus")), PLAYER)
			.hero({5, 8, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER);
		startWithMap(std::move(builder));
		town = expectAt<CGTownInstance>({10, 10, 0});
		seth = findHeroAt({5, 5, 0});
		buyer = findHeroAt({5, 8, 0});
		ASSERT_TRUE(town && seth && buyer);
		town->setVisitingHero(seth);
		town->setGarrisonedHero(buyer);
		buyer->clearSlots();
		town->addBuilding(BuildingID::getDwellingFromLevel(2, 0));
		if(upgraded) town->addBuilding(BuildingID::getDwellingFromLevel(2, 1));
		for(auto & row : town->creatures) row = {0, {}};
		town->creatures[2] = {10, {creature("core:wight")}};
		if(upgraded) town->creatures[2].second.push_back(creature("core:wraith"));
		for(const auto building : {BuildingID::CASTLE, BuildingID::CITADEL, BuildingID::HORDE_1,
			BuildingID::HORDE_2, BuildingID::GRAIL}) town->removeBuilding(building);
		for(int resource = 0; resource < 7; ++resource)
			gameState()->getPlayerState(PLAYER)->resources[resource] = 100000;
		gameState()->day = 1;
		revealMap(PLAYER);
		gateway = makeGateway(PLAYER);
		ASSERT_EQ(town->getUpperArmy(), buyer);
	}
	std::vector<NK2AI::creInfo> forecast(uint8_t turn = 0)
	{
		const auto before = gameState()->saveToMemory();
		const auto rng = rngWire();
		auto result = gateway->nullkiller->armyManager->getArmyAvailableToBuy(
			buyer, town, gateway->cc->getResourceAmount(), turn, buyer);
		EXPECT_EQ(gameState()->saveToMemory(), before);
		EXPECT_EQ(rngWire(), rng);
		return result;
	}
	std::vector<std::byte> rngWire()
	{
		CMemorySerializer memory;
		memory.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		return memory.extractBuffer();
	}
	void fillArmyExceptMatching(CreatureID matching)
	{
		ASSERT_TRUE(buyer->setCreature(SlotID(0), matching, 1000));
		const std::array others{creature("core:skeleton"), creature("core:walkingDead"), creature("core:vampire"),
			creature("core:lich"), creature("core:blackKnight"), creature("core:boneDragon")};
		for(size_t i = 0; i < others.size(); ++i) ASSERT_TRUE(buyer->setCreature(SlotID(i + 1), others[i], 1000));
		ASSERT_EQ(buyer->stacksCount(), GameConstants::ARMY_SIZE);
	}
	void conscription(CGHeroInstance * hero)
	{
		const std::string skill = "new-horizons:estates";
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({skill, skill + ".taxCollector"});
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({skill, skill + ".conscription"});
		ASSERT_TRUE(hero->hasActivePerk(skill, skill + ".conscription"));
	}
};
}

TEST_F(NewHorizonsSethRecruitmentAITest, MummyChoiceUsesOneStockAndExecutesThroughAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mummy = creature("core:mummy");
	ASSERT_NO_FATAL_FAILURE(fillArmyExceptMatching(mummy));
	const auto offer = townRecruitmentOffers::query(*town, 2, PLAYER);
	ASSERT_TRUE(offer);
	ASSERT_TRUE(vstd::contains(offer->choices, mummy));
	ASSERT_TRUE(vstd::contains(offer->choices, creature("core:wight")));
	const auto before = gameState()->saveToMemory();
	const auto candidates = forecast();
	ASSERT_EQ(candidates.size(), 1u);
	const auto & selected = candidates.front();
	EXPECT_EQ(selected.creID, mummy);
	EXPECT_EQ(selected.count, 10);
	EXPECT_EQ(selected.level, 2);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	const auto resources = gameState()->getPlayerState(PLAYER)->resources;
	const auto cost = town->getTotalRecruitmentCost(mummy, selected.count, buyer);
	SethRecruitmentClient client(gameState());
	const auto purchasing = makeGateway(PLAYER, &client);
	purchasing->recruitCreatures(town, buyer);
	ASSERT_EQ(client.requests.size(), 1u);
	EXPECT_EQ(client.requests.front().crid, selected.creID);
	EXPECT_EQ(client.requests.front().amount, selected.count);
	EXPECT_EQ(client.requests.front().level, 2);
	EXPECT_EQ(client.requests.front().tid, town->id);
	EXPECT_EQ(client.requests.front().dst, buyer->id);
	EXPECT_EQ(town->creatures[2].first, 0u);
	EXPECT_EQ(buyer->getStackCount(SlotID(0)), 1010);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resources - cost);
	EXPECT_TRUE(forecast().empty());
}

TEST_F(NewHorizonsSethRecruitmentAITest, RoyalChoiceKeepsOrdinaryAlternativesAndOneGrowthIncrement)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto royal = creature("new-horizons:royalMummy");
	ASSERT_TRUE(royal.hasValue());
	ASSERT_NO_FATAL_FAILURE(fillArmyExceptMatching(royal));
	const auto offer = townRecruitmentOffers::query(*town, 2, PLAYER);
	ASSERT_TRUE(offer);
	for(const auto id : {creature("core:wight"), creature("core:wraith"), creature("core:mummy"), royal})
		ASSERT_TRUE(vstd::contains(offer->choices, id));
	const auto before = gameState()->saveToMemory();
	const auto candidates = forecast(7);
	ASSERT_EQ(candidates.size(), 1u);
	EXPECT_EQ(candidates.front().creID, royal);
	EXPECT_EQ(candidates.front().level, 2);
	EXPECT_EQ(candidates.front().count, 10 + town->creatureGrowth(2));
	EXPECT_EQ(town->creatures[2].first, 10u);
	EXPECT_EQ(gameState()->saveToMemory(), before);
}

TEST_F(NewHorizonsSethRecruitmentAITest, ReceiverDiscountAndOtherRowsShareTheActualResourceBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mummy = creature("core:mummy");
	ASSERT_NO_FATAL_FAILURE(conscription(seth));
	const auto ordinaryCost = town->getTotalRecruitmentCost(mummy, 10, buyer);
	EXPECT_NE(ordinaryCost, town->getTotalRecruitmentCost(mummy, 10, seth));
	ASSERT_NO_FATAL_FAILURE(conscription(buyer));
	const auto discounted = town->getTotalRecruitmentCost(mummy, 10, buyer);
	ASSERT_LT(discounted[EGameResID::GOLD], ordinaryCost[EGameResID::GOLD]);
	ResourceSet budget = discounted + town->getTotalRecruitmentCost(creature("core:skeleton"), 1, buyer);
	gameState()->getPlayerState(PLAYER)->resources = budget;
	town->creatures[0] = {100, {creature("core:skeleton")}};
	const auto before = gameState()->saveToMemory();
	const auto candidates = forecast();
	ResourceSet spent;
	int tombUnits = 0;
	for(const auto & selected : candidates)
	{
		spent += town->getTotalRecruitmentCost(selected.creID, selected.count, buyer);
		if(selected.level == 2) tombUnits += selected.count;
	}
	EXPECT_EQ(tombUnits, 10);
	EXPECT_TRUE(budget.canAfford(spent));
	EXPECT_GE(spent[EGameResID::GOLD], discounted[EGameResID::GOLD]);
	EXPECT_EQ(gameState()->saveToMemory(), before);
}

TEST_F(NewHorizonsSethRecruitmentAITest, DepartureRestoresOrdinaryChoiceAndPreservesSharedRemainder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.recruitCreatures(town->id, buyer->id, creature("core:wight"), 3, 2, PLAYER));
	ASSERT_TRUE(handler.recruitCreatures(town->id, buyer->id, creature("core:mummy"), 2, 2, PLAYER));
	ASSERT_EQ(town->creatures[2].first, 5u);
	town->setVisitingHero(nullptr);
	const auto before = gameState()->saveToMemory();
	const auto candidates = forecast();
	ASSERT_EQ(candidates.size(), 1u);
	EXPECT_EQ(candidates.front().creID, creature("core:wraith"));
	EXPECT_EQ(candidates.front().count, 5);
	EXPECT_EQ(candidates.front().level, 2);
	EXPECT_EQ(gameState()->saveToMemory(), before);
}
