/*
 * NewHorizonsAdventureSpellUnlockAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "lib/CPlayerState.h"
#include "lib/GameConstants.h"
#include "lib/IGameSettings.h"
#include "lib/ResourceSet.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/spells/NewHorizonsMagic.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{
const PlayerColor PLAYER(0);

class RecordingUnlockClient final : public IClient
{
public:
	std::vector<std::pair<ObjectInstanceID, int>> requestedUnlocks;

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor,
		const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & request, PlayerColor, bool) override
	{
		const auto * unlock = dynamic_cast<const UnlockNewHorizonsAdventureSpell *>(&request);
		if(!unlock)
			throw std::runtime_error("Nullkiller submitted an unexpected request during town interaction");

		requestedUnlocks.emplace_back(unlock->townId, unlock->guildLevel);
		return static_cast<int>(requestedUnlocks.size());
	}
};

class NewHorizonsAdventureSpellUnlockAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(useNewHorizonsRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
				JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		else
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PLAYER)
			.town({5, 5, 0}, FactionID::CASTLE, PLAYER)
			.hero({10, 10, 0}, HeroTypeID(0), PLAYER)
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);
		town->setVisitingHero(hero);
	}

	void setResources(const ResourceSet & target)
	{
		const auto current = gameState()->getPlayerState(PLAYER)->resources;
		for(const auto resource : ALL_RESOURCES)
			grantResources(PLAYER, resource, static_cast<int>(target[resource] - current[resource]));
	}

	void interact(RecordingUnlockClient & client)
	{
		auto callback = makeCallback(PLAYER, &client);
		auto gateway = makeGateway(callback);
		gateway->performObjectInteraction(town, NK2AI::HeroPtr(hero, callback.get()));
	}

	static constexpr std::array<GameResID, 7> ALL_RESOURCES = {
		GameResID(EGameResID::WOOD), GameResID(EGameResID::MERCURY), GameResID(EGameResID::ORE),
		GameResID(EGameResID::SULFUR), GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS),
		GameResID(EGameResID::GOLD)
	};

	bool useNewHorizonsRules = true;
	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(NewHorizonsAdventureSpellUnlockAITest, RequestsAllFiveCanonicalUnlocksInGuildOrderWhenAffordable)
{
	startGame();
	const std::array<BuildingID, 5> guildBuildings = {
		BuildingID::MAGES_GUILD_1, BuildingID::MAGES_GUILD_2, BuildingID::MAGES_GUILD_3,
		BuildingID::MAGES_GUILD_4, BuildingID::MAGES_GUILD_5
	};
	ResourceSet totalCost;
	for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
	{
		town->addBuilding(guildBuildings.at(static_cast<size_t>(guildLevel - 1)));
		const auto spell = newHorizonsMagic::adventureSpellForGuildLevel(gameState()->getMagicRules(), guildLevel);
		ASSERT_TRUE(spell.hasValue());
		totalCost += newHorizonsMagic::adventureSpellUnlockCost(gameState()->getMagicRules(), spell);
	}
	setResources(totalCost);

	RecordingUnlockClient client;
	interact(client);

	ASSERT_EQ(client.requestedUnlocks.size(), 5u);
	for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
		EXPECT_EQ(client.requestedUnlocks.at(static_cast<size_t>(guildLevel - 1)), std::make_pair(town->id, guildLevel));
}

TEST_F(NewHorizonsAdventureSpellUnlockAITest, SkipsUnbuiltUnlockedAndUnaffordableTiers)
{
	startGame();
	town->addBuilding(BuildingID::MAGES_GUILD_1);
	town->addBuilding(BuildingID::MAGES_GUILD_3);
	town->addBuilding(BuildingID::MAGES_GUILD_4);
	town->setNewHorizonsAdventureSpellUnlocked(1);
	const auto spell = newHorizonsMagic::adventureSpellForGuildLevel(gameState()->getMagicRules(), 3);
	ASSERT_TRUE(spell.hasValue());
	setResources(newHorizonsMagic::adventureSpellUnlockCost(gameState()->getMagicRules(), spell));

	RecordingUnlockClient client;
	interact(client);

	ASSERT_EQ(client.requestedUnlocks.size(), 1u);
	EXPECT_EQ(client.requestedUnlocks.front(), std::make_pair(town->id, 3));
}

TEST_F(NewHorizonsAdventureSpellUnlockAITest, LegacyMagicRulesDoNotSubmitUnlockRequests)
{
	useNewHorizonsRules = false;
	startGame();
	town->addBuilding(BuildingID::MAGES_GUILD_1);

	RecordingUnlockClient client;
	interact(client);

	EXPECT_TRUE(client.requestedUnlocks.empty());
}
