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

TEST_F(NewHorizonsMagicUniversityTest, OffersAllSchoolsAndRetainsOfferAfterEachPurchase)
{
	startGame();
	const auto schools = newHorizonsMagic::schoolSkills(gameState()->getMagicRules());
	ASSERT_EQ(schools.size(), 6u);

	const auto offers = town->availableItemsIds(EMarketMode::RESOURCE_SKILL);
	ASSERT_EQ(offers.size(), schools.size());
	for(const auto skill : schools)
		EXPECT_TRUE(vstd::contains(offers, skill));

	const auto tuition = newHorizonsUniversity::tuition(town, gameState()->getMagicRules(), gameState()->getSettings());
	EXPECT_TRUE(newHorizonsUniversity::usesNewHorizonsTuition(town, gameState()->getMagicRules()));
	EXPECT_EQ(tuition[EGameResID::GOLD], 5000);
	EXPECT_EQ(tuition[EGameResID::MERCURY], 2);
	EXPECT_EQ(tuition[EGameResID::SULFUR], 2);
	EXPECT_EQ(tuition[EGameResID::CRYSTAL], 2);
	EXPECT_EQ(tuition[EGameResID::GEMS], 2);

	grant(tuition * 2);
	const auto resourcesBefore = gameState()->getPlayerState(PlayerColor(0))->resources;
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.buySecSkill(town, hero, schools[0]));
	EXPECT_EQ(hero->getSecSkillLevel(schools[0]), MasteryLevel::BASIC);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resourcesBefore - tuition);
	EXPECT_EQ(town->availableItemsIds(EMarketMode::RESOURCE_SKILL).size(), 6u);
	ASSERT_TRUE(handler.buySecSkill(town, hero, schools[1]));
	EXPECT_EQ(hero->getSecSkillLevel(schools[1]), MasteryLevel::BASIC);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resourcesBefore - tuition * 2);
	EXPECT_EQ(town->availableItemsIds(EMarketMode::RESOURCE_SKILL).size(), 6u);
}

TEST_F(NewHorizonsMagicUniversityTest, InsufficientRareResourceDoesNotPartiallyCharge)
{
	startGame();
	const auto school = newHorizonsMagic::schoolSkills(gameState()->getMagicRules()).front();
	const auto tuition = newHorizonsUniversity::tuition(town, gameState()->getMagicRules(), gameState()->getSettings());
	TResources available = tuition;
	available[EGameResID::MERCURY]--;
	grant(-gameState()->getPlayerState(PlayerColor(0))->resources);
	grant(available);
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;

	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	EXPECT_FALSE(handler.buySecSkill(town, hero, school));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
	EXPECT_EQ(hero->getSecSkillLevel(school), MasteryLevel::NONE);
}

TEST_F(NewHorizonsMagicUniversityTest, FullSecondarySkillRosterBlocksPurchaseWithoutCharging)
{
	startGame();
	const auto school = newHorizonsMagic::schoolSkills(gameState()->getMagicRules()).front();
	const int skillLimit = gameState()->getSettings().getInteger(EGameSettings::HEROES_SKILL_PER_HERO);
	int added = 0;
	for(int index = 0; index < LIBRARY->skillh->size() && added < skillLimit; ++index)
	{
		const SecondarySkill candidate(index);
		if(candidate == school)
			continue;
		hero->setSecSkillLevel(candidate, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		++added;
	}
	ASSERT_EQ(hero->getSecSkillLevel(school), MasteryLevel::NONE);
	ASSERT_FALSE(hero->canLearnSkill());

	const auto tuition = newHorizonsUniversity::tuition(town, gameState()->getMagicRules(), gameState()->getSettings());
	grant(tuition);
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	EXPECT_FALSE(handler.buySecSkill(town, hero, school));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
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
