/*
 * NewHorizonsUniqueBuildingTrainingTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../server/CGameHandler.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsUniqueBuildingTrainingTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	static FactionID faction(const char * id)
	{
		return FactionID(FactionID::decode(id));
	}

	static HeroTypeID heroType(const char * id)
	{
		return HeroTypeID(HeroTypeID::decode(id));
	}
};
}

TEST_F(NewHorizonsUniqueBuildingTrainingTest, FourBuildingsTrainOncePerHeroPerTownAndPersist)
{
	const auto castle = faction("core:castle");
	const auto inferno = faction("core:inferno");
	const auto dungeon = faction("core:dungeon");
	const auto stronghold = faction("core:stronghold");

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(48, false).playerActive(PlayerColor(0))
		.town({5, 12, 0}, castle, PlayerColor(0))
		.town({13, 12, 0}, castle, PlayerColor(0))
		.town({21, 12, 0}, inferno, PlayerColor(0))
		.town({29, 12, 0}, dungeon, PlayerColor(0))
		.town({37, 12, 0}, stronghold, PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{CreatureID(0), 1}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->getLeadershipCapacity());
	auto towns = findAll<CGTownInstance>();
	ASSERT_EQ(towns.size(), 5u);

	CGTownInstance * castleOne = nullptr;
	CGTownInstance * castleTwo = nullptr;
	CGTownInstance * infernoTown = nullptr;
	CGTownInstance * dungeonTown = nullptr;
	CGTownInstance * strongholdTown = nullptr;
	for(auto * town : towns)
	{
		if(town->getFactionID() == castle && !castleOne)
			castleOne = town;
		else if(town->getFactionID() == castle)
			castleTwo = town;
		else if(town->getFactionID() == inferno)
			infernoTown = town;
		else if(town->getFactionID() == dungeon)
			dungeonTown = town;
		else if(town->getFactionID() == stronghold)
			strongholdTown = town;
	}
	ASSERT_NE(castleOne, nullptr);
	ASSERT_NE(castleTwo, nullptr);
	ASSERT_NE(infernoTown, nullptr);
	ASSERT_NE(dungeonTown, nullptr);
	ASSERT_NE(strongholdTown, nullptr);
	ASSERT_TRUE(dungeonTown->rewardableBuildings.contains(BuildingID::SPECIAL_2));
	EXPECT_EQ(dungeonTown->rewardableBuildings.at(BuildingID::SPECIAL_2)->configuration.getResetDuration(
		gameState()->getCalendar()), 0u);

	const int leadershipBefore = hero->valOfBonuses(BonusType::LEADERSHIP);
	const auto leadershipCapacityBefore = hero->getLeadershipCapacity()->capacity;
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const auto pikemanCapacityBefore = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanCapacityBefore);
	const int attackBefore = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const int spellPowerBefore = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	const int knowledgeBefore = hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	CGTownInstance * activeTown = nullptr;

	auto visit = [&](CGTownInstance * town, BuildingID building)
	{
		ASSERT_TRUE(town->rewardableBuildings.contains(building));
		town->addBuilding(building);
		if(activeTown)
			activeTown->setVisitingHero(nullptr);
		town->setVisitingHero(hero);
		gameHandler.heroVisitCastle(town, hero);
		EXPECT_TRUE(town->rewardableBuildings.at(building)->wasVisited(hero));
		activeTown = town;
	};

	visit(castleOne, BuildingID::SPECIAL_3);
	EXPECT_EQ(hero->valOfBonuses(BonusType::LEADERSHIP), leadershipBefore + 100);
	EXPECT_EQ(hero->getLeadershipCapacity()->capacity, leadershipCapacityBefore + 100);
	ASSERT_TRUE(hero->getLeadershipSlotCapacity(pikeman));
	EXPECT_EQ(hero->getLeadershipSlotCapacity(pikeman)->leadership,
		pikemanCapacityBefore->leadership + 100);

	// A second physical Brotherhood trains the same hero again.
	visit(castleTwo, BuildingID::SPECIAL_3);
	EXPECT_EQ(hero->valOfBonuses(BonusType::LEADERSHIP), leadershipBefore + 200);
	EXPECT_EQ(hero->getLeadershipCapacity()->capacity, leadershipCapacityBefore + 200);

	// Repeating the first physical building is denied by its serialized visitor set.
	visit(castleOne, BuildingID::SPECIAL_3);
	EXPECT_EQ(hero->valOfBonuses(BonusType::LEADERSHIP), leadershipBefore + 200);

	visit(infernoTown, BuildingID::SPECIAL_4);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), spellPowerBefore + 5);
	visit(dungeonTown, BuildingID::SPECIAL_2);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), knowledgeBefore + 5);
	visit(strongholdTown, BuildingID::SPECIAL_4);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), attackBefore + 5);

	const auto castleOneID = castleOne->id;
	const auto castleTwoID = castleTwo->id;
	const auto infernoID = infernoTown->id;
	const auto dungeonID = dungeonTown->id;
	const auto strongholdID = strongholdTown->id;
	const auto heroID = hero->id;
	if(activeTown)
		activeTown->setVisitingHero(nullptr);
	const auto saved = gameState()->saveToMemory();

	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(heroID);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->valOfBonuses(BonusType::LEADERSHIP), leadershipBefore + 200);
	EXPECT_EQ(restoredHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), spellPowerBefore + 5);
	EXPECT_EQ(restoredHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), knowledgeBefore + 5);
	EXPECT_EQ(restoredHero->getPrimSkillLevel(PrimarySkill::ATTACK), attackBefore + 5);

	EXPECT_TRUE(restored.getTown(castleOneID)->rewardableBuildings.at(BuildingID::SPECIAL_3)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(castleTwoID)->rewardableBuildings.at(BuildingID::SPECIAL_3)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(infernoID)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(dungeonID)->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(strongholdID)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredHero));
}
