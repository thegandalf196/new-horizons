/*
 * NewHorizonsMagicUniversityTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/mapObjects/CGMarket.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../server/CGameHandler.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsMagicUniversityTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.town({5, 5, 0}, FactionID::CONFLUX, PlayerColor(0))
			.hero({10, 10, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);
		// The simulation authors stock during town initialization, before the
		// building exists. Constructing it later must only expose that stock;
		// client and AI reads must never generate gameplay state.
		ASSERT_EQ(town->getHouseOfWisdomScrolls().size(), 6u);
		town->addBuilding(BuildingID::SPECIAL_2);
	}

	void grant(const TResources & amount)
	{
		for(const auto resource : {GameResID(EGameResID::MERCURY), GameResID(EGameResID::SULFUR),
			GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS), GameResID(EGameResID::GOLD)})
			if(amount[resource])
				grantResources(PlayerColor(0), resource, amount[resource]);
	}

	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(NewHorizonsMagicUniversityTest, HouseOfWisdomOffersSpellScrollsAndNoSecondarySkills)
{
	startGame();
	const auto offers = town->availableItemsIds(EMarketMode::RESOURCE_SKILL);
	ASSERT_EQ(offers.size(), 6u);
	for(const auto & offer : offers)
	{
		const auto spell = offer.as<SpellID>();
		EXPECT_TRUE(spell.hasValue());
		EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), spell));
		EXPECT_FALSE(offer.as<SecondarySkill>().hasValue());
	}
	for(const auto school : newHorizonsMagic::schoolSkills(gameState()->getMagicRules()))
		EXPECT_FALSE(vstd::contains(offers, school));
}

TEST_F(NewHorizonsMagicUniversityTest, PurchaseDeductsGoldGrantsScrollAndRemovesOffer)
{
	startGame();
	const auto spell = town->availableItemsIds(EMarketMode::RESOURCE_SKILL).front().as<SpellID>();
	const auto price = newHorizonsHouseOfWisdom::price(spell);
	grant(price * 2);
	const auto resourcesBefore = gameState()->getPlayerState(PlayerColor(0))->resources;
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.buyHouseOfWisdomScroll(town, hero, spell));
	EXPECT_TRUE(hero->hasScroll(spell, false));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resourcesBefore - price);
	EXPECT_EQ(town->availableItemsIds(EMarketMode::RESOURCE_SKILL).size(), 5u);
	for(const auto school : newHorizonsMagic::schoolSkills(gameState()->getMagicRules()))
		EXPECT_EQ(hero->getSecSkillLevel(school), MasteryLevel::NONE);
}

TEST_F(NewHorizonsMagicUniversityTest, InsufficientGoldDoesNotPartiallyCharge)
{
	startGame();
	const auto spell = town->availableItemsIds(EMarketMode::RESOURCE_SKILL).front().as<SpellID>();
	const auto price = newHorizonsHouseOfWisdom::price(spell);
	TResources available = price;
	available[EGameResID::GOLD]--;
	grant(-gameState()->getPlayerState(PlayerColor(0))->resources);
	grant(available);
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;

	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	EXPECT_FALSE(handler.buyHouseOfWisdomScroll(town, hero, spell));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_FALSE(hero->hasScroll(spell, false));
}

TEST_F(NewHorizonsMagicUniversityTest, StockPersistsAcrossSaveAndLoad)
{
	startGame();
	const auto before = town->availableItemsIds(EMarketMode::RESOURCE_SKILL);
	const auto saved = gameState()->saveToMemory();

	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(town->id);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredTown->availableItemsIds(EMarketMode::RESOURCE_SKILL), before);
}

TEST(NewHorizonsMagicUniversityPriceTest, LegacyUniversityRemainsGoldOnly)
{
	const JsonNode legacyRules;
	const auto price = newHorizonsUniversity::tuition(nullptr, legacyRules, *LIBRARY->settingsHandler);
	EXPECT_EQ(price[EGameResID::GOLD], 2000);
	EXPECT_EQ(price[EGameResID::MERCURY], 0);
	EXPECT_EQ(price[EGameResID::SULFUR], 0);
	EXPECT_EQ(price[EGameResID::CRYSTAL], 0);
	EXPECT_EQ(price[EGameResID::GEMS], 0);
}
