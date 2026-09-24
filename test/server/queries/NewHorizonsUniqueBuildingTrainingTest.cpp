/*
 * NewHorizonsUniqueBuildingTrainingTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/processors/NewTurnProcessor.h"
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

TEST_F(NewHorizonsUniqueBuildingTrainingTest, MagiUseShooterMeleePenaltyWithoutLosingRangedAbilities)
{
	for(const auto * id : {"core:mage", "core:archMage"})
	{
		const auto * creature = CreatureID(CreatureID::decode(id)).toCreature();
		ASSERT_NE(creature, nullptr);
		EXPECT_FALSE(creature->hasBonusOfType(BonusType::NO_MELEE_PENALTY)) << id;
		EXPECT_TRUE(creature->hasBonusOfType(BonusType::SHOOTER)) << id;
		EXPECT_TRUE(creature->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY)) << id;
		EXPECT_TRUE(creature->hasBonusOfType(BonusType::CHANGES_SPELL_COST_FOR_ALLY)) << id;
	}
	EXPECT_TRUE(CreatureID(CreatureID::decode("core:archMage")).toCreature()
		->hasBonusOfType(BonusType::NO_WALL_PENALTY));
}

TEST_F(NewHorizonsUniqueBuildingTrainingTest, TrainingPersistsAndAstralNexusAlwaysRefillsToNormalMaximum)
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

	const int leadershipBefore = hero->valOfBonuses(BonusType::LEADERSHIP);
	const auto leadershipCapacityBefore = hero->getLeadershipCapacity()->capacity;
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const auto pikemanCapacityBefore = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanCapacityBefore);
	const int attackBefore = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const int spellPowerBefore = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
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
		if(town->getFactionID() == dungeon && building == BuildingID::SPECIAL_2)
			EXPECT_FALSE(town->rewardableBuildings.at(building)->wasVisited(hero));
		else
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
	const int manaLimitBeforeVisit = hero->manaLimit();
	ASSERT_GT(manaLimitBeforeVisit, 7);
	gameHandler.setManaPoints(hero->id, manaLimitBeforeVisit - 7);
	ASSERT_EQ(hero->getManaAvailable(), manaLimitBeforeVisit - 7);
	visit(dungeonTown, BuildingID::SPECIAL_2);
	EXPECT_EQ(hero->getManaAvailable(), hero->manaLimit());
	EXPECT_EQ(hero->getManaAvailable(), manaLimitBeforeVisit);
	gameHandler.setManaPoints(hero->id, manaLimitBeforeVisit - 3);
	ASSERT_EQ(hero->getManaAvailable(), manaLimitBeforeVisit - 3);
	visit(dungeonTown, BuildingID::SPECIAL_2);
	EXPECT_EQ(hero->getManaAvailable(), hero->manaLimit());
	gameHandler.setManaPoints(hero->id, manaLimitBeforeVisit + 11);
	ASSERT_EQ(hero->getManaAvailable(), manaLimitBeforeVisit + 11);
	visit(dungeonTown, BuildingID::SPECIAL_2);
	EXPECT_EQ(hero->getManaAvailable(), hero->manaLimit());
	EXPECT_EQ(hero->getManaAvailable(), manaLimitBeforeVisit);
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
	EXPECT_EQ(restoredHero->getManaAvailable(), restoredHero->manaLimit());
	EXPECT_EQ(restoredHero->getPrimSkillLevel(PrimarySkill::ATTACK), attackBefore + 5);

	EXPECT_TRUE(restored.getTown(castleOneID)->rewardableBuildings.at(BuildingID::SPECIAL_3)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(castleTwoID)->rewardableBuildings.at(BuildingID::SPECIAL_3)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(infernoID)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredHero));
	EXPECT_FALSE(restored.getTown(dungeonID)->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredHero));
	EXPECT_TRUE(restored.getTown(strongholdID)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredHero));
}

TEST_F(NewHorizonsUniqueBuildingTrainingTest, ArcaneReservoirAllowsExactlyOneHeroPerWeek)
{
	const auto tower = faction("core:tower");

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(48, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({20, 20, 0}, tower, PlayerColor(0))
		.town({35, 20, 0}, faction("core:inferno"), PlayerColor(1))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.hero({7, 5, 0}, heroType("core:tyris"), PlayerColor(0))
		.hero({35, 5, 0}, heroType("core:valeska"), PlayerColor(1));
	startWithMap(std::move(builder));

	auto towns = findAll<CGTownInstance>();
	ASSERT_EQ(towns.size(), 2u);
	CGTownInstance * town = nullptr;
	for(auto * candidate : towns)
		if(candidate->getOwner() == PlayerColor(0))
			town = candidate;
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::SPECIAL_4);
	ASSERT_TRUE(town->rewardableBuildings.contains(BuildingID::SPECIAL_4));
	auto * reservoir = town->rewardableBuildings.at(BuildingID::SPECIAL_4).get();
	ASSERT_EQ(reservoir->configuration.visitMode, Rewardable::VISIT_ONCE);
	ASSERT_TRUE(reservoir->configuration.resetParameters.visitors);
	ASSERT_EQ(reservoir->configuration.getResetDuration(gameState()->getCalendar()), 7u);

	auto * firstHero = findHeroAt({5, 5, 0});
	auto * secondHero = findHeroAt({7, 5, 0});
	ASSERT_NE(firstHero, nullptr);
	ASSERT_NE(secondHero, nullptr);
	ASSERT_NE(firstHero, secondHero);

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	const int firstManaLimit = firstHero->manaLimit();
	ASSERT_GT(firstManaLimit, 3);
	town->setVisitingHero(firstHero);
	gameHandler.setManaPoints(firstHero->id, firstManaLimit - 3);
	ASSERT_TRUE(gameHandler.visitTownBuilding(town->id, BuildingID::SPECIAL_4));
	EXPECT_EQ(firstHero->getManaAvailable(), firstManaLimit * 2);
	EXPECT_TRUE(reservoir->wasVisited(firstHero));

	// VISIT_ONCE is shared by the physical building, so a different hero is
	// refused until the weekly visitor set is cleared.
	town->setVisitingHero(nullptr);
	town->setVisitingHero(secondHero);
	const int secondManaLimit = secondHero->manaLimit();
	ASSERT_GT(secondManaLimit, 4);
	gameHandler.setManaPoints(secondHero->id, secondManaLimit - 4);
	ASSERT_TRUE(gameHandler.visitTownBuilding(town->id, BuildingID::SPECIAL_4));
	EXPECT_EQ(secondHero->getManaAvailable(), secondManaLimit - 4);

	// The reset fires when the authoritative NewTurn packet advances the map
	// from day 7 to day 8, i.e. at the start of the next week.
	for(int day = 0; day < 8; ++day)
		gameHandler.onNewTurn();
	EXPECT_EQ(gameState()->day, 8u);
	EXPECT_EQ(gameState()->players.at(PlayerColor(1)).status, EPlayerStatus::INGAME);
	EXPECT_FALSE(reservoir->wasVisited(firstHero));

	town->setVisitingHero(nullptr);
	town->setVisitingHero(secondHero);
	gameHandler.setManaPoints(secondHero->id, secondManaLimit - 2);
	ASSERT_TRUE(gameHandler.visitTownBuilding(town->id, BuildingID::SPECIAL_4));
	EXPECT_EQ(secondHero->getManaAvailable(), secondManaLimit * 2);
	EXPECT_TRUE(reservoir->wasVisited(secondHero));
}

TEST_F(NewHorizonsUniqueBuildingTrainingTest, WeeklySpecialRumorFallsBackWhenNoPlayersCanBeRanked)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(48, false).playerActive(PlayerColor(0))
		.town({20, 20, 0}, faction("core:castle"), PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));

	ASSERT_FALSE(LIBRARY->generaltexth->findStringsWithPrefix("core.randtvrn").empty());
	auto player = gameState()->players.find(PlayerColor(0));
	ASSERT_NE(player, gameState()->players.end());
	player->second.status = EPlayerStatus::LOSER;

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	// With the first-turn Castle fixture, no other weekly path draws randomness
	// before pickNewRumor. Find a seed whose first draw selects TYPE_SPECIAL, then
	// reset it so this test exercises the empty thief-guild ranking fallback.
	int specialRumorSeed = -1;
	for(int candidateSeed = 1; candidateSeed <= 50000; ++candidateSeed)
	{
		gameHandler.randomizer->setSeed(candidateSeed);
		if(gameHandler.getRandomGenerator().nextInt64(0, 3) == 1)
		{
			specialRumorSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(specialRumorSeed, -1);
	gameHandler.randomizer->setSeed(specialRumorSeed);
	gameHandler.newTurnProcessor->onNewTurn();

	EXPECT_EQ(gameState()->currentRumor.type, RumorState::TYPE_RAND);
}

TEST_F(NewHorizonsUniqueBuildingTrainingTest, TowerLibraryOnlyAddsMageGrowthAndBrimstoneProducesSulfur)
{
	const auto tower = faction("core:tower");
	const auto inferno = faction("core:inferno");

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(48, false).playerActive(PlayerColor(0))
		.town({20, 20, 0}, tower, PlayerColor(0))
		.town({30, 20, 0}, inferno, PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));

	CGTownInstance * towerTown = nullptr;
	CGTownInstance * infernoTown = nullptr;
	for(auto * town : findAll<CGTownInstance>())
	{
		if(town->getFactionID() == tower)
			towerTown = town;
		else if(town->getFactionID() == inferno)
			infernoTown = town;
	}
	ASSERT_NE(towerTown, nullptr);
	ASSERT_NE(infernoTown, nullptr);

	towerTown->addBuilding(BuildingID::DWELL_LVL_4);
	towerTown->addBuilding(BuildingID::DWELL_LVL_4_UP);
	towerTown->addBuilding(BuildingID::DWELL_LVL_5);
	towerTown->addBuilding(BuildingID::DWELL_LVL_5_UP);

	const auto mage = CreatureID(CreatureID::decode("core:mage"));
	const auto archMage = CreatureID(CreatureID::decode("core:archMage"));
	const auto genie = CreatureID(CreatureID::decode("core:genie"));
	const auto masterGenie = CreatureID(CreatureID::decode("core:masterGenie"));
	const auto rowForUpgrades = [&](CreatureID base, CreatureID upgraded)
	{
		return std::find_if(towerTown->getTown()->creatures.begin(), towerTown->getTown()->creatures.end(),
			[base, upgraded](const auto & row)
			{
				return std::find(row.begin(), row.end(), base) != row.end()
					&& std::find(row.begin(), row.end(), upgraded) != row.end();
			});
	};
	const auto mageRow = rowForUpgrades(mage, archMage);
	const auto genieRow = rowForUpgrades(genie, masterGenie);
	ASSERT_NE(mageRow, towerTown->getTown()->creatures.end());
	ASSERT_NE(genieRow, towerTown->getTown()->creatures.end());
	const int mageLevel = static_cast<int>(std::distance(towerTown->getTown()->creatures.begin(), mageRow));
	const int genieLevel = static_cast<int>(std::distance(towerTown->getTown()->creatures.begin(), genieRow));
	ASSERT_NE(mageLevel, genieLevel);

	// addBuilding is intentionally a bare fixture helper; model the currently
	// offered base and upgraded creatures explicitly, regardless of tier order.
	const auto growthWith = [&](int level, std::vector<CreatureID> available)
	{
		towerTown->creatures[level].second = std::move(available);
		return towerTown->getGrowthInfo(level).totalGrowth();
	};
	const int mageGrowthBefore = growthWith(mageLevel, {mage});
	const int archMageGrowthBefore = growthWith(mageLevel, {mage, archMage});
	const int genieGrowthBefore = growthWith(genieLevel, {genie});
	const int masterGenieGrowthBefore = growthWith(genieLevel, {genie, masterGenie});

	towerTown->addBuilding(BuildingID::SPECIAL_3);
	EXPECT_EQ(growthWith(mageLevel, {mage}), mageGrowthBefore + 1);
	EXPECT_EQ(growthWith(mageLevel, {mage, archMage}), archMageGrowthBefore + 1);
	EXPECT_EQ(growthWith(genieLevel, {genie}), genieGrowthBefore);
	EXPECT_EQ(growthWith(genieLevel, {genie, masterGenie}), masterGenieGrowthBefore);

	const auto sulfurBefore = infernoTown->dailyIncome()[EGameResID::SULFUR];
	infernoTown->addBuilding(BuildingID::SPECIAL_2);
	EXPECT_EQ(infernoTown->dailyIncome()[EGameResID::SULFUR], sulfurBefore + 1);
}
