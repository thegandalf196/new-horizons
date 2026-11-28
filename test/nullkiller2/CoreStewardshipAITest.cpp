/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NullkillerTest.h"
#include "../../AI/Nullkiller2/EstatesAdministration.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameSettings.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/IClient.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/networkPacks/EstatesPacks.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/GameHandlerTestServer.h"

namespace
{
const PlayerColor PLAYER(0);
class StewardshipClient final : public IClient
{
	GameHandlerTestServer server;
	CGameHandler handler;
	int sequence = 0;
public:
	std::vector<AssignTownSteward> requests;
	explicit StewardshipClient(const std::shared_ptr<CGameState> & state)
		: server(state, PLAYER), handler(server, state) { state->actingPlayers.insert(PLAYER); }
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto pack = CMemorySerializer::deepCopy(request);
		pack->player = player;
		pack->requestID = ++sequence;
		if(const auto * assignment = dynamic_cast<const AssignTownSteward *>(pack.get())) requests.push_back(*assignment);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *pack);
		return sequence;
	}
};

class CoreStewardshipAITest : public NullkillerTest
{
protected:
	CGHeroInstance * clavius = nullptr;
	CGHeroInstance * ordinary = nullptr;
	CGTownInstance * necropolis = nullptr;
	CGTownInstance * castle = nullptr;
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.town({7, 7, 0}, FactionID::NECROPOLIS, PLAYER).townGarrison({})
			.town({7, 20, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
			.hero({30, 30, 0}, HeroTypeID(HeroTypeID::decode("core:clavius")), PLAYER)
			.hero({30, 26, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER);
		startWithMap(std::move(builder));
		clavius = findHeroAt({30, 30, 0});
		ordinary = findHeroAt({30, 26, 0});
		necropolis = expectAt<CGTownInstance>({7, 7, 0});
		castle = expectAt<CGTownInstance>({7, 20, 0});
		ASSERT_TRUE(clavius && ordinary && necropolis && castle);
		necropolis->creatures = {{0, {CreatureID(CreatureID::decode("core:skeleton"))}}};
		NewStructures structures;
		structures.tid = castle->id;
		structures.bid = {BuildingID::CAPITOL};
		gameState()->apply(structures);
		ASSERT_TRUE(castle->hasBuilt(BuildingID::CAPITOL));
		ASSERT_GT(castle->dailyIncome()[EGameResID::GOLD], necropolis->dailyIncome()[EGameResID::GOLD]);
	}
};
}

TEST_F(CoreStewardshipAITest, ClaviusTakesPoorerNecropolisBeforeGenericRichTownAssignment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	StewardshipClient client(gameState());
	auto callback = makeCallback(PLAYER, &client);
	NK2AI::appointAvailableStewards(*callback, PLAYER);
	ASSERT_EQ(client.requests.size(), 2u);
	EXPECT_EQ(client.requests.front().hero, clavius->id);
	EXPECT_EQ(client.requests.front().town, necropolis->id);
	EXPECT_EQ(newHorizonsEstates::townSteward(*necropolis), clavius);
	EXPECT_EQ(newHorizonsEstates::townSteward(*castle), ordinary);
	NK2AI::appointAvailableStewards(*callback, PLAYER);
	EXPECT_EQ(client.requests.size(), 2u);
}

TEST_F(CoreStewardshipAITest, StewardshipPerkGetsRichestTownBeforeEarlierOrdinaryHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	clavius->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
	const auto estates = SecondarySkill(SecondarySkill::decode("new-horizons:estates"));
	ordinary->setSecSkillLevel(estates, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ordinary->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.taxCollector"});
	ordinary->setSecSkillLevel(estates, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ordinary->applyPerkSelection({"new-horizons:estates", "new-horizons:estates.stewardship"});
	ASSERT_TRUE(ordinary->hasActivePerk("new-horizons:estates", "new-horizons:estates.stewardship"));
	StewardshipClient client(gameState());
	auto callback = makeCallback(PLAYER, &client);
	NK2AI::appointAvailableStewards(*callback, PLAYER);
	ASSERT_EQ(client.requests.size(), 2u);
	EXPECT_EQ(client.requests.front().hero, ordinary->id);
	EXPECT_EQ(client.requests.front().town, castle->id);
	EXPECT_EQ(newHorizonsEstates::townSteward(*castle), ordinary);
	EXPECT_EQ(newHorizonsEstates::townSteward(*necropolis), clavius);
}
