/*
 * NewHorizonsAdventureSpellUnlockTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <array>

#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsAdventureSpellUnlockTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
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
			.playerActive(PlayerColor(0))
			.town({5, 5, 0}, FactionID::CONFLUX, PlayerColor(0))
			.hero({10, 10, 0}, HeroTypeID(0), PlayerColor(0))
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.hero({12, 10, 0}, HeroTypeID(1), PlayerColor(0))
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		heroes = findAll<CGHeroInstance>();
		ASSERT_NE(town, nullptr);
		ASSERT_EQ(heroes.size(), 2u);
	}

	void setResources(const ResourceSet & target)
	{
		const auto current = gameState()->getPlayerState(PlayerColor(0))->resources;
		for(const auto resource : ALL_RESOURCES)
			grantResources(PlayerColor(0), resource, static_cast<int>(target[resource] - current[resource]));
	}

	static constexpr std::array<GameResID, 7> ALL_RESOURCES = {
		GameResID(EGameResID::WOOD), GameResID(EGameResID::MERCURY), GameResID(EGameResID::ORE),
		GameResID(EGameResID::SULFUR), GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS),
		GameResID(EGameResID::GOLD)
	};

	bool useNewHorizonsRules = true;
	CGTownInstance * town = nullptr;
	std::vector<CGHeroInstance *> heroes;
};
}

TEST_F(NewHorizonsAdventureSpellUnlockTest, PurchasesAllFiveTownUnlocksTeachesVisitorsAndPersists)
{
	startGame();
	const std::array<BuildingID, 5> guildBuildings = {
		BuildingID::MAGES_GUILD_1, BuildingID::MAGES_GUILD_2, BuildingID::MAGES_GUILD_3,
		BuildingID::MAGES_GUILD_4, BuildingID::MAGES_GUILD_5
	};
	town->setVisitingHero(heroes.front());
	town->setGarrisonedHero(heroes.back());

	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
	{
		town->addBuilding(guildBuildings.at(static_cast<size_t>(guildLevel - 1)));
		const auto spell = newHorizonsMagic::adventureSpellForGuildLevel(gameState()->getMagicRules(), guildLevel);
		ASSERT_TRUE(spell.hasValue()) << "Missing canonical Adventure Spell for Guild tier " << guildLevel;
		if(guildLevel == 1)
		{
			// A malformed/legacy persisted town spell list must not bypass the
			// explicit town-unlock path.
			town->spells.at(0) = {spell};
			town->newHorizonsMageGuildVisibleSpells.at(0) = 1;
			handler.giveSpells(town, heroes.front(), true);
			EXPECT_FALSE(heroes.front()->getSpellsInSpellbook().contains(spell));
		}
		const auto cost = newHorizonsMagic::adventureSpellUnlockCost(gameState()->getMagicRules(), spell);
		setResources(cost);
		const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;

		ASSERT_TRUE(handler.unlockNewHorizonsAdventureSpell(town->id, guildLevel));
		EXPECT_TRUE(town->hasNewHorizonsAdventureSpellUnlocked(guildLevel));
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before - cost);
		EXPECT_TRUE(heroes.front()->getSpellsInSpellbook().contains(spell));
		EXPECT_FALSE(heroes.back()->getSpellsInSpellbook().contains(spell));

		const auto afterPurchase = gameState()->getPlayerState(PlayerColor(0))->resources;
		EXPECT_FALSE(handler.unlockNewHorizonsAdventureSpell(town->id, guildLevel));
		EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, afterPurchase);
	}

	town->setGarrisonedHero(nullptr);
	town->setVisitingHero(nullptr);
	town->setVisitingHero(heroes.back());
	handler.visitCastleObjects(town, heroes.back());
	for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
	{
		const auto spell = newHorizonsMagic::adventureSpellForGuildLevel(gameState()->getMagicRules(), guildLevel);
		EXPECT_TRUE(heroes.back()->getSpellsInSpellbook().contains(spell));
	}

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(town->id);
	ASSERT_NE(restoredTown, nullptr);
	for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
		EXPECT_TRUE(restoredTown->hasNewHorizonsAdventureSpellUnlocked(guildLevel));
}

TEST_F(NewHorizonsAdventureSpellUnlockTest, RejectsUnbuiltGuildTierAndInsufficientResourcesWithoutMutation)
{
	startGame();
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());

	const auto spell = newHorizonsMagic::adventureSpellForGuildLevel(gameState()->getMagicRules(), 1);
	ASSERT_TRUE(spell.hasValue());
	const auto cost = newHorizonsMagic::adventureSpellUnlockCost(gameState()->getMagicRules(), spell);
	setResources(cost);
	const auto beforeMissingGuild = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_FALSE(handler.unlockNewHorizonsAdventureSpell(town->id, 1));
	EXPECT_FALSE(town->hasNewHorizonsAdventureSpellUnlocked(1));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, beforeMissingGuild);

	town->addBuilding(BuildingID::MAGES_GUILD_1);
	auto insufficient = cost;
	auto reduced = false;
	for(const auto resource : ALL_RESOURCES)
	{
		if(insufficient[resource] > 0)
		{
			--insufficient[resource];
			reduced = true;
			break;
		}
	}
	ASSERT_TRUE(reduced);
	setResources(insufficient);
	const auto beforeUnaffordable = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_FALSE(handler.unlockNewHorizonsAdventureSpell(town->id, 1));
	EXPECT_FALSE(town->hasNewHorizonsAdventureSpellUnlocked(1));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, beforeUnaffordable);
}

TEST_F(NewHorizonsAdventureSpellUnlockTest, LegacyWorldCannotUseNewHorizonsUnlockPath)
{
	useNewHorizonsRules = false;
	startGame();
	town->addBuilding(BuildingID::MAGES_GUILD_1);

	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_FALSE(handler.unlockNewHorizonsAdventureSpell(town->id, 1));
	EXPECT_FALSE(town->hasNewHorizonsAdventureSpellUnlocked(1));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
}

TEST(NewHorizonsAdventureSpellUnlockWireTest, RequestAndResultTypesRoundTripThroughPolymorphicRegistration)
{
	UnlockNewHorizonsAdventureSpell request(ObjectInstanceID(17), 4);
	const auto restoredRequest = CMemorySerializer::deepCopy<CPackForServer>(request);
	const auto * typedRequest = dynamic_cast<const UnlockNewHorizonsAdventureSpell *>(restoredRequest.get());
	ASSERT_NE(typedRequest, nullptr);
	EXPECT_EQ(typedRequest->townId, ObjectInstanceID(17));
	EXPECT_EQ(typedRequest->guildLevel, 4);

	SetNewHorizonsAdventureSpellUnlock result;
	result.townId = ObjectInstanceID(23);
	result.guildLevel = 2;
	const auto restoredResult = CMemorySerializer::deepCopy<CPackForClient>(result);
	const auto * typedResult = dynamic_cast<const SetNewHorizonsAdventureSpellUnlock *>(restoredResult.get());
	ASSERT_NE(typedResult, nullptr);
	EXPECT_EQ(typedResult->townId, ObjectInstanceID(23));
	EXPECT_EQ(typedResult->guildLevel, 2);
}
